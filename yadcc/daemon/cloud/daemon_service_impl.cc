// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this
// file except in compliance with the License. You may obtain a copy of the
// License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "yadcc/daemon/cloud/daemon_service_impl.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "flare/base/enum.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"
#include "flare/rpc/logging.h"
#include "flare/rpc/rpc_client_controller.h"
#include "flare/rpc/rpc_server_controller.h"

#include "yadcc/api/scheduler.flare.pb.h"
#include "yadcc/daemon/cloud/compiler_registry.h"
#include "yadcc/daemon/cloud/execution_engine.h"
#include "yadcc/daemon/cloud/remote_task/cxx_compilation_task.h"
#include "yadcc/daemon/common_flags.h"
#include "yadcc/daemon/sysinfo.h"

using namespace std::literals;

DEFINE_int32(cpu_load_average_seconds, 15,
             "This option controls the how long should we average CPU usage to "
             "determine load on this machine.");

// Declaring this flag here doesn't seem right, TBH.
DECLARE_string(servant_priority);

namespace yadcc::daemon::cloud {

DaemonServiceImpl::DaemonServiceImpl(std::string network_location)
    : network_location_(std::move(network_location)) {
  FLARE_LOG_INFO("Serving at [{}].", network_location_);

  // `expires_in` passed to `Heartbeat` must be greater than timer interval to
  // a certain degree. This is required to compensate possible network delay (or
  // other delays).
  pacemaker_ = flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), 1s,
                                      [this] { Heartbeat(10s); });
}

void DaemonServiceImpl::QueueCxxCompilationTask(
    const QueueCxxCompilationTaskRequest& request,
    QueueCxxCompilationTaskResponse* response,
    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(flare::EndpointGetIp(controller->GetRemotePeer()));

  if (!IsTokenAcceptable(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  // Prepare the task.
  auto task = flare::MakeRefCounted<CxxCompilationTask>();
  if (auto status = task->Prepare(request, controller->GetRequestAttachment());
      !status.ok()) {
    controller->SetFailed(status.code(), status.message());
    return;
  }

  // Submit the command to our execution engine.
  auto task_id =
      ExecutionEngine::Instance()->TryQueueTask(request.task_grant_id(), task);
  if (!task_id) {
    controller->SetFailed(STATUS_HEAVILY_LOADED,
                          flare::Format("Too many compilation tasks in queue. "
                                        "Rejecting new tasks actively."));
    return;
  }

  // Indeed we can wait for sometime before completing the RPC. If compilation
  // completes fast enough, we can avoid a dedicated `WaitForCompilationOutput`
  // call. This can help improve overall performance.
  //
  // For the moment I'm not sure it's worthwhile, though. Given that the task is
  // submitted to us, it's likely would take some time to finish (otherwise it's
  // more preferable to be performed locally instead of on the cloud).
  //
  // TODO(luobogao): Let's see if the decision should be revised.

  // Fill the response the return back to our caller.
  response->set_task_id(*task_id);
}

void DaemonServiceImpl::ReferenceTask(const ReferenceTaskRequest& request,
                                      ReferenceTaskResponse* response,
                                      flare::RpcServerController* controller) {
  if (!IsTokenAcceptable(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  if (!ExecutionEngine::Instance()->TryReferenceTask(request.task_id())) {
    controller->SetFailed(STATUS_TASK_NOT_FOUND);
  }
  return;
}

void DaemonServiceImpl::WaitForCompilationOutput(
    const WaitForCompilationOutputRequest& request,
    WaitForCompilationOutputResponse* response,
    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(flare::EndpointGetIp(controller->GetRemotePeer()));

  if (!IsTokenAcceptable(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  constexpr auto kMaximumWaitableTime = 10s;
  auto desired_wait = request.milliseconds_to_wait() * 1ms;

  // For the moment support for Zstd is mandatory.
  if (std::find(request.acceptable_compression_algorithms().begin(),
                request.acceptable_compression_algorithms().end(),
                COMPRESSION_ALGORITHM_ZSTD) ==
      request.acceptable_compression_algorithms().end()) {
    controller->SetFailed("Invalid arguments. Support for Zstd is mandatory.");
    return;
  }

  // Let's see if the job is done.
  auto output =
      ExecutionEngine::Instance()->WaitForTask(request.task_id(), desired_wait);
  if (!output) {
    auto code = output.error();
    if (code == ExecutionStatus::Failed) {
      response->set_status(COMPILATION_TASK_STATUS_FAILED);
    } else if (code == ExecutionStatus::Running) {
      response->set_status(COMPILATION_TASK_STATUS_RUNNING);
    } else if (code == ExecutionStatus::NotFound) {
      response->set_status(COMPILATION_TASK_STATUS_NOT_FOUND);
    } else {
      FLARE_UNREACHABLE("Unrecognized error [{}].",
                        flare::underlying_value(code));
    }
    return;
  }

  // It is, fill the response.
  auto task = static_cast<RemoteTask*>(output->Get());
  response->set_status(COMPILATION_TASK_STATUS_DONE);
  response->set_exit_code(task->GetExitCode());
  response->set_output(task->GetStandardOutput());
  response->set_error(task->GetStandardError());
  response->set_compression_algorithm(COMPRESSION_ALGORITHM_ZSTD);
  *response->mutable_extra_info() = task->GetExtraInfo();
  controller->SetResponseAttachment(task->GetOutputFilePack());
}

void DaemonServiceImpl::FreeTask(const FreeTaskRequest& request,
                                 FreeTaskResponse* response,
                                 flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(flare::EndpointGetIp(controller->GetRemotePeer()));

  if (!IsTokenAcceptable(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  ExecutionEngine::Instance()->FreeTask(request.task_id());  // Let it go.
}

void DaemonServiceImpl::Stop() {
  flare::fiber::KillTimer(pacemaker_);  // The heart will not go on.
  Heartbeat(0ns);
}

void DaemonServiceImpl::Join() {}

void DaemonServiceImpl::Heartbeat(std::chrono::nanoseconds expires_in) {
  static const std::unordered_map<std::string, scheduler::ServantPriority>
      kServantPriorities = {
          {"dedicated", scheduler::SERVANT_PRIORITY_DEDICATED},
          {"user", scheduler::SERVANT_PRIORITY_USER},
      };

  scheduler::SchedulerService_SyncStub stub(FLAGS_scheduler_uri);
  scheduler::HeartbeatRequest req;
  flare::RpcClientController ctlr;

  req.set_token(FLAGS_token);
  req.set_next_heartbeat_in_ms(expires_in / 1ms);
  req.set_version(version_for_upgrade);
  req.set_location(network_location_);
  req.set_servant_priority(kServantPriorities.at(FLAGS_servant_priority));
  req.set_memory_available_in_bytes(GetMemoryAvailable());
  req.set_total_memory_in_bytes(GetTotalMemory());
  if (auto capacity = ExecutionEngine::Instance()->GetMaximumTasks()) {
    req.set_capacity(*capacity);
  } else {
    req.set_capacity(0);
    req.set_not_accepting_task_reason(
        flare::underlying_value(capacity.error()));
  }
  req.set_num_processors(GetNumberOfProcessors());
  // If our try is failed, we get 1min loadavg instead.
  req.set_current_load(TryGetProcessorLoad(FLAGS_cpu_load_average_seconds * 1s)
                           .value_or(GetProcessorLoadInLastMinute()));
  for (auto&& e : CompilerRegistry::Instance()->EnumerateEnvironments()) {
    *req.add_env_descs() = e;
  }
  for (auto&& e : ExecutionEngine::Instance()->EnumerateTasks()) {
    auto running_task_info = req.add_running_tasks();
    running_task_info->set_servant_location(network_location_);
    running_task_info->set_task_grant_id(e.task_grant_id);
    running_task_info->set_servant_task_id(e.servant_task_id);
    running_task_info->set_task_digest(
        static_cast<RemoteTask*>(e.task.Get())->GetDigest());
  }
  auto result = stub.Heartbeat(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING("Failed to send heartbeat to scheduler.");
    return;
  }

  // Were any tasks not known to the scheduler, kill it.
  ExecutionEngine::Instance()->KillExpiredTasks(
      {result->expired_tasks().begin(), result->expired_tasks().end()});
  // Update acceptable tokens.
  UpdateAcceptableTokens(
      {result->acceptable_tokens().begin(), result->acceptable_tokens().end()});
}

bool DaemonServiceImpl::IsTokenAcceptable(const std::string& token) {
  std::shared_lock _(token_lock_);
  return token_verifier_->Verify(token);
}

void DaemonServiceImpl::UpdateAcceptableTokens(
    std::unordered_set<std::string> tokens) {
  // Well we don't have to create a new `TokenVerifier` each time, it's only
  // required when tokens actually changes.
  //
  // For the sake of simplicity, it's kept so for now.
  std::scoped_lock _(token_lock_);
  token_verifier_ = std::make_unique<TokenVerifier>(std::move(tokens));
}

}  // namespace yadcc::daemon::cloud
