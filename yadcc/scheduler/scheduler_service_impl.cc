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

#include "yadcc/scheduler/scheduler_service_impl.h"

#include <chrono>
#include <string>

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/openssl/rand.h"

#include "flare/base/chrono.h"
#include "flare/base/compression.h"
#include "flare/base/encoding.h"
#include "flare/base/logging.h"
#include "flare/rpc/logging.h"
#include "flare/rpc/rpc_server_controller.h"

#include "yadcc/scheduler/task_dispatcher.h"

using namespace std::literals;

DEFINE_int32(min_daemon_version, 0,
             "Daemons whose version are older than this option won't be "
             "accepted by this scheduler.");
DEFINE_int32(serving_daemon_token_rollout_interval, 3600,
             "Interval in seconds between new token to access serving daemon "
             "is rolled out.");

namespace yadcc::scheduler {

namespace {

std::string NextServingDaemonToken() {
  char buf[16];
  FLARE_CHECK_EQ(RAND_bytes(reinterpret_cast<unsigned char*>(buf), sizeof(buf)),
                 1);
  return flare::EncodeHex(std::string_view(buf, 16));
}

}  // namespace

SchedulerServiceImpl::SchedulerServiceImpl() {
  active_serving_daemon_tokens_ = {NextServingDaemonToken(),
                                   NextServingDaemonToken(),
                                   NextServingDaemonToken()};
  next_serving_daemon_token_rollout_ =
      flare::ReadCoarseSteadyClock() +
      FLAGS_serving_daemon_token_rollout_interval * 1s;
}

void SchedulerServiceImpl::Heartbeat(const HeartbeatRequest& request,
                                     HeartbeatResponse* response,
                                     flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  // Verify the requestor first.
  if (!token_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }
  if (request.version() < FLAGS_min_daemon_version) {
    controller->SetFailed(STATUS_VERSION_TOO_OLD);
    return;
  }

  // The address observed by ourselves is authoritative.
  std::string observed_location, reported_location;

  // Basic sanity check.
  if (auto opt = flare::TryParse<flare::Endpoint>(request.location()); !opt) {
    FLARE_LOG_ERROR(
        "Misbehaving daemon: Reporting invalid network location [{}].",
        request.location());
    controller->SetFailed(STATUS_INVALID_ARGUMENT);
    return;
  } else {
    // Respect port reported by the servant.
    if (auto&& remote = controller->GetRemotePeer();
        remote.Family() == AF_INET) {
      observed_location = flare::Format("{}:{}", flare::EndpointGetIp(remote),
                                        flare::EndpointGetPort(*opt));
    } else {
      FLARE_CHECK_EQ(remote.Family(), AF_INET6,
                     "Not recognized address family [{}].", remote.Family());
      observed_location = flare::Format("[{}]:{}", flare::EndpointGetIp(remote),
                                        flare::EndpointGetPort(*opt));
    }
    reported_location = opt->ToString();  // Normalizes IP.
  }
  // We don't treat mismatch between `observed_location` and `reported_location`
  // as an error. In case the servant is behind NAT, they can indeed differ.
  //
  // For the moment servant behind NAT does not serve requests from others. In
  // our development environment this is acceptable. Most of NAT-ed nodes are
  // containerized environment, and can supply few idle CPUs to others.
  //
  // FIXME: Perhaps we shouldn't send heartbeat from these nodes to scheduler at
  // all.

  auto expires_in = request.next_heartbeat_in_ms() * 1ms;
  if (expires_in > 30s) {  // Be sane.
    controller->SetFailed(STATUS_INVALID_ARGUMENT);
    return;
  }

  // Notify the dispatcher about this heartbeat then.
  ServantPersonality servant;
  servant.version = request.version();
  servant.observed_location = observed_location;
  servant.reported_location = reported_location;
  servant.current_load = request.current_load();
  servant.num_processors = request.num_processors();
  if (servant.num_processors == 0) {
    // Older daemon does not report number of processors present, so fake a
    // value here.
    servant.num_processors = request.capacity();
  }
  servant.total_memory_in_bytes = request.total_memory_in_bytes();
  servant.memory_available_in_bytes = request.memory_available_in_bytes();
  servant.priority = request.servant_priority();
  if (servant.priority == SERVANT_PRIORITY_UNKNOWN ||
      !ServantPriority_IsValid(servant.priority) /* How come? */) {
    // Older servant. Default to "user" then.
    servant.priority = SERVANT_PRIORITY_USER;
  }
  servant.max_tasks = request.capacity();
  servant.not_accepting_task_reason = request.not_accepting_task_reason();
  if (observed_location != reported_location) {
    // It's likely the servant is behind NAT.
    //
    // Unless we have some NAT traversal mechanism, servants behind NAT is not
    // reachable from outside. So don't assign tasks to them.
    servant.max_tasks = 0;
    servant.not_accepting_task_reason = NOT_ACCEPTING_TASK_REASON_BEHIND_NAT;
  }
  for (auto&& e : request.env_descs()) {
    servant.environments.push_back(e);
  }

  // Well by our definition, if `expires_in` is 0, the servant should be removed
  // immediately. Here we don't take that specially. Instead, we're relying on
  // `TaskDispatcher`'s expiration timer to remove the servant as a later time.
  //
  // To prevent allocating further tasks to this servant, we mark it as
  // capacity-full.
  if (expires_in == 0ns) {
    servant.max_tasks = 0;
  }
  TaskDispatcher::Instance()->KeepServantAlive(servant, expires_in);

  // All tokens that are possibly alive should be accepted by the servants.
  for (auto&& e : DetermineActiveServingDaemonTokens()) {
    response->add_acceptable_tokens(e);
  }

  // Sent back the task the daemon should be running on. Any tasks not listed
  // here will likely be killed by the daemon.
  for (auto&& e : TaskDispatcher::Instance()->ExamineRunningTasks(
           request.location(),
           {request.running_tasks().begin(), request.running_tasks().end()})) {
    response->add_expired_tasks(e);
  }

  // It's not always a sign of error, `response->expired_tasks().empty()` can be
  // non-empty because of the following reasons:
  //
  // - Network delay. The task has actually end normally, it just was there by
  //   the time the heartbeat was sent and has gone since then.
  //
  // - The task has expired.
}

void SchedulerServiceImpl::GetConfig(const GetConfigRequest& request,
                                     GetConfigResponse* response,
                                     flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  // Verify the requestor first.
  if (!token_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }
  response->set_serving_daemon_token(DetermineActiveServingDaemonTokens()[1]);
}

void SchedulerServiceImpl::WaitForStartingTask(
    const WaitForStartingTaskRequest& request,
    WaitForStartingTaskResponse* response,
    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  // Verify the requestor first.
  if (!token_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  auto max_wait = request.milliseconds_to_wait() * 1ms;
  auto next_keep_alive = request.next_keep_alive_in_ms() * 1ms;
  if (max_wait > 10s || next_keep_alive > 30s) {
    controller->SetFailed(STATUS_INVALID_ARGUMENT);
    return;
  }

  TaskPersonality task;
  task.requestor_ip = flare::EndpointGetIp(controller->GetRemotePeer());
  task.env_desc = request.env_desc();

  for (int i = 0; i != request.immediate_reqs(); ++i) {
    auto result = TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, next_keep_alive,
        // Note that we can't wait except for the first task. If we wait for too
        // long for the remaining tasks, the first task may have already been
        // expired before we even return.
        i == 0 ? max_wait : 0s, false);
    if (!result) {
      break;
    }
    auto&& added = response->add_grants();
    added->set_task_grant_id(result->task_id);
    added->set_servant_location(result->servant_location);
  }
  for (int i = 0; i != request.prefetch_reqs(); ++i) {
    auto result = TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, next_keep_alive, response->grants().empty() ? max_wait : 0s,
        true);
    if (!result) {
      break;
    }
    auto&& added = response->add_grants();
    added->set_task_grant_id(result->task_id);
    added->set_servant_location(result->servant_location);
  }

  if (response->grants().empty()) {
    controller->SetFailed(STATUS_NO_QUOTA_AVAILABLE,
                          "The compilation cloud is busy now.");
    return;
  }
}

void SchedulerServiceImpl::KeepTaskAlive(
    const KeepTaskAliveRequest& request, KeepTaskAliveResponse* response,
    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  // Verify the requestor first.
  if (!token_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  auto next_keep_alive = request.next_keep_alive_in_ms() * 1ms;
  if (next_keep_alive > 30s) {
    controller->SetFailed(STATUS_INVALID_ARGUMENT);
    return;
  }

  for (auto&& e : request.task_grant_ids()) {
    response->add_statuses(
        TaskDispatcher::Instance()->KeepTaskAlive(e, next_keep_alive));
  }
}

void SchedulerServiceImpl::FreeTask(const FreeTaskRequest& request,
                                    FreeTaskResponse* response,
                                    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  // Verify the requestor first.
  if (!token_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  for (auto&& e : request.task_grant_ids()) {
    TaskDispatcher::Instance()->FreeTask(e);
  }
}

std::vector<std::string>
SchedulerServiceImpl::DetermineActiveServingDaemonTokens() {
  std::scoped_lock _(lock_);
  auto now = flare::ReadCoarseSteadyClock();
  if (next_serving_daemon_token_rollout_ < now) {
    next_serving_daemon_token_rollout_ =
        now + FLAGS_serving_daemon_token_rollout_interval * 1s;
    active_serving_daemon_tokens_.pop_front();
    active_serving_daemon_tokens_.push_back(NextServingDaemonToken());
  }
  FLARE_CHECK_EQ(active_serving_daemon_tokens_.size(), 3);
  return {active_serving_daemon_tokens_.begin(),
          active_serving_daemon_tokens_.end()};
}

}  // namespace yadcc::scheduler
