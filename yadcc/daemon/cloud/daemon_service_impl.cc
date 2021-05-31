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

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "flare/base/chrono.h"
#include "flare/base/compression.h"
#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding.h"
#include "flare/base/enum.h"
#include "flare/base/internal/cpu.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"
#include "flare/rpc/logging.h"
#include "flare/rpc/rpc_client_controller.h"
#include "flare/rpc/rpc_server_controller.h"

#include "yadcc/api/scheduler.flare.pb.h"
#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/cloud/compiler_registry.h"
#include "yadcc/daemon/cloud/distributed_cache_writer.h"
#include "yadcc/daemon/cloud/execution_engine.h"
#include "yadcc/daemon/cloud/sysinfo.h"
#include "yadcc/daemon/common_flags.h"

using namespace std::literals;

DEFINE_int32(cpu_load_average_seconds, 15,
             "This option controls the how long should we average CPU usage to "
             "determine load on this machine.");

// Declaring this flag here doesn't seem right, TBH.
DECLARE_string(servant_priority);

namespace yadcc::daemon::cloud {
namespace {

struct JobContext {
  std::mutex lock;  // Protects concurrent call to `PrepareForRead()`.

  EnvironmentDesc env;
  std::string invocation_arguments;
  std::string source_digest;  // Used for updating compilation cache.
  flare::NoncontiguousBuffer object_file;

  // To cope with possible RPC retry, do NOT read it yourself, call
  // `PrepareForRead()` and read `object_file` instead.
  TemporaryFile temporary_object_file{FLAGS_temporary_dir};

  void PrepareForRead() {
    std::scoped_lock _(lock);

    if (temporary_object_file.fd()) {
      object_file = temporary_object_file.ReadAll();
      temporary_object_file.Close();
    }
  }
};

flare::Compressor* GetZstdCompressor() {
  thread_local auto compressor = flare::MakeCompressor("zstd");
  return compressor.get();
}

flare::Decompressor* GetZstdDecompressor() {
  thread_local auto decompressor = flare::MakeDecompressor("zstd");
  return decompressor.get();
}

}  // namespace

DaemonServiceImpl::DaemonServiceImpl(std::string network_location)
    : network_location_(std::move(network_location)) {
  FLARE_LOG_INFO("Serving at [{}].", network_location_);
  // `expires_in` passed to `Heartbeat` must be greater than timer interval to
  // a certain degree. This is required to compensate possible network delay (or
  // other delays).
  pacemaker_ = flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), 1s,
                                      [this] { Heartbeat(10s); });
}

DaemonServiceImpl::~DaemonServiceImpl() {}

void DaemonServiceImpl::QueueCompilationTask(
    const QueueCompilationTaskRequest& request,
    QueueCompilationTaskResponse* response,
    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(flare::EndpointGetIp(controller->GetRemotePeer()));

  if (!IsTokenAcceptable(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  // Let's figure out which compiler should be used.
  auto compiler =
      CompilerRegistry::Instance()->TryGetCompilerPath(request.env_desc());
  if (!compiler) {
    controller->SetFailed(
        STATUS_ENVIRONMENT_NOT_AVAILABLE,
        "The requested environment is not available at this server.");
    return;
  }

  if (request.compression_algorithm() != COMPRESSION_ALGORITHM_ZSTD) {
    controller->SetFailed("Compression algorithm not supported.");
    return;
  }

  FLARE_LOG_WARNING_IF_ONCE(request.disallow_cache_fill(),
                            "Not implemented: Disallowing cache fill.");

  std::string source_digest;

  // TODO(luobogao): We can decompress the input on-the-fly to save some memory.
  TemporaryFile standard_input(FLAGS_temporary_dir);
  auto decompressed_source = flare::Decompress(
      GetZstdDecompressor(), controller->GetRequestAttachment());
  if (!decompressed_source) {
    controller->SetFailed("Failed to decompress source code.");
    return;
  }
  standard_input.Write(*decompressed_source);

  // Initialize execution context.
  auto job_context = std::make_shared<JobContext>();
  job_context->env = request.env_desc();
  job_context->invocation_arguments = request.invocation_arguments();
  // Hashing the source code ourselves (as opposed to relying on user to provide
  // it). "Defensive programming".
  job_context->source_digest =
      flare::EncodeHex(flare::Blake3(*decompressed_source));

  // TODO(luobogao): Do some basic security checks on the invocation arguments
  // to prevent threats such as data leak (by specifying path to a sensitive
  // file as source input) etc.
  //
  // Or can we use some jail facility to accomplish this?
  auto cmd =
      flare::Format("{} {} -o {}", *compiler, request.invocation_arguments(),
                    job_context->temporary_object_file.GetPath());

  // Submit the command to our execution engine.
  auto task_id = ExecutionEngine::Instance()->TryQueueCommandForExecution(
      request.task_grant_id(), cmd,
      ExecutionEngine::Input{
          .standard_input = std::move(standard_input),
          .standard_output = TemporaryFile{FLAGS_temporary_dir},
          .standard_error = TemporaryFile{FLAGS_temporary_dir},
          .context = job_context});
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
  response->set_status(COMPILATION_TASK_STATUS_RUNNING);
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
  auto output = ExecutionEngine::Instance()->WaitForCompletion(
      request.task_id(), desired_wait);
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
  response->set_status(COMPILATION_TASK_STATUS_DONE);
  response->set_exit_code(output->exit_code);
  // `stdout` / `stderr` should be small (or better yet, empty) for most of
  // times as we don't expect compilation to fail (or with warning) too often.
  response->set_output(flare::FlattenSlow(output->standard_output));
  response->set_error(flare::FlattenSlow(output->standard_error));
  response->set_compression_algorithm(COMPRESSION_ALGORITHM_ZSTD);

  if (output->exit_code == 0) {  // Deal with the resulting object file then.
    // FIXME: Most of the things below should be done immediately upon task
    // completion, instead of here.

    auto context = static_cast<JobContext*>(output->context.get());
    context->PrepareForRead();

    // TODO(luobogao): We can compress the result immediately after compilation
    // finishes to save memory.
    //
    // FIXME: We'd recompress the object file and waste CPU cycles if the RPC is
    // retried.
    auto compressed_bytes =
        flare::Compress(GetZstdCompressor(), context->object_file);
    FLARE_CHECK(compressed_bytes);  // How can compression fail?

    // This doesn't look quite right. I'm sure we'd refactor it out later, to
    // update the cache once compilation is done (instead of upon being waited
    // by the submitter.). But let's get it working first.
    //
    // FIXME: On RPC retry we'll update the cache more than once.
    // FIXME: How to disable caching here?
    auto cache_key = GetCacheEntryKey(
        context->env, context->invocation_arguments, context->source_digest);
    (void)DistributedCacheWriter::Instance()->AsyncWrite(
        cache_key, response->exit_code(), response->output(), response->error(),
        *compressed_bytes);  // NOT waited

    controller->SetResponseAttachment(*compressed_bytes);
  }
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
    // Hmmm..
    req.set_not_accepting_task_reason(
        static_cast<scheduler::NotAcceptingTaskReason>(
            flare::underlying_value(capacity.error())));
  }
  req.set_num_processors(GetNumberOfProcessors());
  // If our try is failed, we get 1min loadavg instead.
  req.set_current_load(TryGetProcessorLoad(FLAGS_cpu_load_average_seconds * 1s)
                           .value_or(GetProcessorLoadInLastMinute()));
  for (auto&& e : CompilerRegistry::Instance()->EnumerateEnvironments()) {
    *req.add_env_descs() = e;
  }
  for (auto&& e : ExecutionEngine::Instance()->EnumerateGrantOfRunningTask()) {
    req.add_running_tasks(e);
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
