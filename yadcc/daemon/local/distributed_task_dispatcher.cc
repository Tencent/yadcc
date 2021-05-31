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

#include "yadcc/daemon/local/distributed_task_dispatcher.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "thirdparty/gflags/gflags.h"

#include "flare/base/enum.h"
#include "flare/base/internal/time_view.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/string.h"
#include "flare/fiber/async.h"
#include "flare/fiber/this_fiber.h"
#include "flare/fiber/timer.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/common_flags.h"
#include "yadcc/daemon/local/distributed_cache_reader.h"

using namespace std::literals;

// TODO(luobogao): Introduce `DEFINE_int32(max_running_tasks)` to limit max
// running tasks submitted to the compilation cloud. This can help reduce the
// possibility of overloading the cluster.

DEFINE_string(debugging_always_use_servant_at, "",
              "For debugging / testing purpose only. If this flag is set, "
              "servant assigned to us by the scheduler is ignored, and the one "
              "specified here is used instead. Note that URI (instead of "
              "IP:port) should be used here.");

namespace yadcc::daemon::local {

namespace {

std::string FormatTime(const flare::internal::SystemClockView& view) {
  auto time = std::chrono::system_clock::to_time_t(view.Get());
  struct tm buf;
  FLARE_CHECK(localtime_r(&time, &buf));
  char buffer[256];
  FLARE_CHECK(strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &buf));
  return buffer;
}

std::uint64_t NextTaskId() {
  static std::atomic<std::uint64_t> next_id{};
  return ++next_id;
}

// Tests if the given process ID still exists.
//
// FIXME: Same method appears in `local_task_monitor.cc`
bool IsProcessAlive(pid_t pid) {
  struct stat buf;
  // FIXME: What about non-`ENOENT` error?
  return lstat(flare::Format("/proc/{}/", pid).c_str(), &buf) == 0;
}

}  // namespace

DistributedTaskDispatcher* DistributedTaskDispatcher::Instance() {
  static flare::NeverDestroyed<DistributedTaskDispatcher> dispatcher;
  return dispatcher.Get();
}

DistributedTaskDispatcher::DistributedTaskDispatcher()
    : scheduler_stub_(FLAGS_scheduler_uri),
      internal_exposer_("yadcc/distributed_task_dispatcher",
                        [this] { return DumpInternals(); }) {
  abort_timer_ = flare::fiber::SetTimer(1s, [this] { OnAbortTimer(); });
  keep_alive_timer_ =
      flare::fiber::SetTimer(1s, [this] { OnKeepAliveTimer(); });
  kill_orphan_timer_ =
      flare::fiber::SetTimer(1s, [this] { OnKillOrphanTimer(); });
  cleanup_timer_ = flare::fiber::SetTimer(1s, [this] { OnCleanupTimer(); });
  config_keeper_.Start();
}

DistributedTaskDispatcher::~DistributedTaskDispatcher() {
  flare::fiber::KillTimer(abort_timer_);
  flare::fiber::KillTimer(keep_alive_timer_);
  flare::fiber::KillTimer(cleanup_timer_);
}

std::uint64_t DistributedTaskDispatcher::QueueTask(
    CompilationTask task,
    std::chrono::steady_clock::time_point start_deadline) {
  auto task_id = NextTaskId();
  auto desc = flare::MakeRefCounted<TaskDesc>();
  desc->task_id = task_id;
  desc->state = TaskState::Pending;
  desc->task = task;
  desc->start_deadline = start_deadline;
  desc->started_at = flare::ReadCoarseSteadyClock();

  // Kick it off.
  std::scoped_lock _(tasks_lock_);
  tasks_[task_id] = desc;

  // TODO(luobogao): Save the fiber so that we can wait for its completion on
  // leave.
  flare::Fiber([this, desc] { PerformOneTask(desc); }).detach();
  return desc->task_id;
}

DistributedTaskDispatcher::WaitStatus DistributedTaskDispatcher::WaitForTask(
    std::uint64_t task_id, std::chrono::nanoseconds timeout,
    CompilationOutput* output) {
  flare::RefPtr<TaskDesc> desc;
  {
    std::scoped_lock _(tasks_lock_);
    if (auto iter = tasks_.find(task_id); iter != tasks_.end()) {
      desc = iter->second;
    }
  }
  if (!desc) {
    return WaitStatus::NotFound;
  }
  if (!desc->completion_latch.wait_for(timeout)) {
    return WaitStatus::Timeout;
  }

  {
    std::scoped_lock _(desc->lock);
    *output = desc->output;
  }

  // On success, forget about the task.
  {
    std::scoped_lock _(tasks_lock_);
    // Note that this can fail, if `OnCleanupTimer` removes the task before we
    // grab the task lock.
    (void)tasks_.erase(task_id);
  }
  return WaitStatus::OK;
}

void DistributedTaskDispatcher::Stop() {
  task_grant_keeper_.Stop();
  config_keeper_.Stop();
}

void DistributedTaskDispatcher::Join() {
  task_grant_keeper_.Join();
  config_keeper_.Join();

  // FIXME: We should wait until all outstanding operations finish (e.g., fibers
  // for performing task).
}

// TODO(luobogao): What about error handling? Or should we ask the client itself
// to retry on non-compiler error?
void DistributedTaskDispatcher::PerformOneTask(flare::RefPtr<TaskDesc> task) {
  {
    // Fail the task by default.
    //
    // Note that this does not harm. Unless we transit the task to corresponding
    // state (i.e., "dispatched" / "completed"), these fields won't be read.
    std::scoped_lock _(task->lock);
    task->output.exit_code = -126;  // FIXME: Use constant instead.
  }

  // Mark the task as completed and wake up waiter (if any) on leave.
  flare::ScopedDeferred _([&] {
    std::scoped_lock _(task->lock);
    task->state = TaskState::Done;  // `OnCleanupTimer` will take care of this
                                    // task if no one else would.
    task->completed_at = flare::ReadCoarseSteadyClock();

    task->completion_latch.count_down();
    FLARE_VLOG(1, "Task {} has completed.", task->task_id);
  });

  // Let's see if the cache can satisfy our task.
  auto cache_entry =
      task->task.cache_control == CacheControl::Allow
          ? DistributedCacheReader::Instance()->TryRead(GetCacheEntryKey(
                task->task.env_desc, task->task.invocation_arguments,
                task->task.source_digest))
          : std::nullopt;
  if (cache_entry) {  // Our lucky day.
    task->output =
        CompilationOutput{.exit_code = 0,
                          .standard_output = cache_entry->standard_output,
                          .standard_error = cache_entry->standard_error,
                          // FIXME: Decompression.
                          .object_file = cache_entry->object_file};
    return;
  }

  // Wait until we can dispatch the task.
  std::optional<GrantDesc> task_grant;
  while (!task_grant && !task->aborted.load(std::memory_order_relaxed)) {
    task_grant = task_grant_keeper_.Get(task->task.env_desc, 1s);
  }
  if (!task_grant) {
    FLARE_LOG_ERROR("Task {} cannot be started in time. Aborted.",
                    task->task_id);
    return;
  }
  {
    std::scoped_lock _(task->lock);  // For updating task state.

    // Now it's ready to fire.
    //
    // Note that we need to mark the task as "ready" before submitting it (which
    // can take long). If the task submission takes a long time it's possible
    // that by the time the submission is done, the task grant has already
    // expired.
    task->ready_at = flare::ReadCoarseSteadyClock();
    task->last_keep_alive_at = flare::ReadCoarseSteadyClock();
    task->state = TaskState::ReadyToFire;
    task->task_grant_id = task_grant->grant_id;
    task->servant_location = task_grant->servant_location;
  }

  flare::ScopedDeferred __(
      [&] { task_grant_keeper_.Free(task_grant->grant_id); });

  // Create a channel to the servant.
  cloud::DaemonService_SyncStub stub(
      FLAGS_debugging_always_use_servant_at.empty()
          ? flare::Format("flare://{}", task_grant->servant_location)
          : FLAGS_debugging_always_use_servant_at);

  // Now dispatch the task.
  auto servant_task_id =
      SubmitTaskToServant(task_grant->grant_id, *task, &stub);
  if (!servant_task_id) {
    FLARE_LOG_ERROR("Failed to submit task {} to servant [{}]: {}",
                    task->task_id, task_grant->servant_location,
                    servant_task_id.error().ToString());
    // If we have task's ID in hand we actually can fall-though here. Even if
    // the RPC times out, the submission could have nonetheless succeeded. In
    // this case it's only the response had been delayed (or dropped).
    return;
  }
  {
    std::scoped_lock _(task->lock);  // For updating task state.

    task->dispatched_at = flare::ReadCoarseSteadyClock();
    task->state = TaskState::Dispatched;
    task->servant_task_id = *servant_task_id;
  }

  flare::ScopedDeferred ___([&] {
    // Now we can free the task info kept by the remote daemon.
    //
    // TODO(luobogao): Move this out of the critical path to speed things up.
    // Perhaps we can use a free-task queue to do this asynchronously.
    cloud::FreeTaskRequest req;
    flare::RpcClientController ctlr;
    req.set_token(config_keeper_.GetServingDaemonToken());
    req.set_task_id(*servant_task_id);
    (void)stub.FreeTask(req, &ctlr);  // Failure is ignored.
  });

  // We tolerance at most so many **successive** wait failure.
  constexpr auto kRpcRetries = 4;  // Timeout is 3s, up to 120s.
  // Wait until the task completes.
  std::size_t retries_left = kRpcRetries;
  while (retries_left-- && !task->aborted.load(std::memory_order_relaxed)) {
    auto wait_result = WaitServantForTask(*servant_task_id, &stub);
    if (!wait_result) {
      if (wait_result.error() == ServantWaitStatus::Running) {
        // Not an error, actually.
        retries_left = kRpcRetries;
        continue;
      } else if (wait_result.error() == ServantWaitStatus::RpcError) {
        // Transient error, let's see if we have budget to retry.
        if (retries_left) {
          FLARE_LOG_WARNING_EVERY_SECOND(
              "RPC failure in waiting for task {} running on [{}]. {} retries "
              "left.",
              task->task_id, task_grant->servant_location, retries_left);
        } else {
          FLARE_LOG_ERROR(
              "RPC failure in waiting for task {} running on [{}]. Bailing "
              "out.",
              task->task_id, task_grant->servant_location);
        }
        flare::this_fiber::SleepFor(1s);  // Relax.
        continue;
      } else if (wait_result.error() == ServantWaitStatus::Failed) {
        // Permanent error then.
        FLARE_LOG_ERROR("Failed to wait on task {} running on [{}].",
                        task->task_id, task_grant->servant_location);
        std::scoped_lock _(task->lock);
        task->output.exit_code = -125;  // FIXME: Use constant instead.
        break;
      }
      FLARE_UNREACHABLE();
    }

    // If the command finishes with 127, it's likely that we failed to run it.
    //
    // TODO(luobogao): Raise a warning here.
    if (wait_result->exit_code == 127) {
      FLARE_LOG_WARNING_EVERY_SECOND(
          "Failed to start compiler on servant [{}]: {}",
          task_grant->servant_location, wait_result->standard_error);
      // Fall-through.
    }

    // Life is good.
    std::scoped_lock _(task->lock);
    task->output = *wait_result;
    // TODO(luobaogao): We can immediately wake up waiter (if any) by now.
    break;
  }
}

flare::Expected<std::uint64_t, flare::Status>
DistributedTaskDispatcher::SubmitTaskToServant(
    std::uint64_t grant_id, const TaskDesc& task,
    cloud::DaemonService_SyncStub* to) {
  cloud::QueueCompilationTaskRequest req;
  req.set_token(config_keeper_.GetServingDaemonToken());
  req.set_task_grant_id(grant_id);
  *req.mutable_env_desc() = task.task.env_desc;
  req.set_source_path(task.task.source_path);
  req.set_invocation_arguments(task.task.invocation_arguments);
  req.set_compression_algorithm(cloud::COMPRESSION_ALGORITHM_ZSTD);
  req.set_disallow_cache_fill(task.task.cache_control ==
                              CacheControl::Disallow);

  flare::RpcClientController ctlr;
  // This can take long if servant is in a DC that locates in a district
  // different than us.
  ctlr.SetTimeout(30s);
  ctlr.SetRequestAttachment(task.task.preprocessed_source);
  // FIXME: `preprocessed_source` can be freed as soon as we finished submitting
  // it.

  auto result = to->QueueCompilationTask(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING("Rpc failed after {} seconds.",
                      ctlr.GetElapsedTime() / 1s);
    return result.error();
  }
  // For the moment we don't support returning "DONE" on submission.
  if (result->status() != cloud::COMPILATION_TASK_STATUS_RUNNING) {
    FLARE_LOG_ERROR_EVERY_SECOND("Unexpected task status [{}] from servant.",
                                 result->status());
    return flare::Status{-1, "Unexpected task status from servant."};
  }
  return result->task_id();
}

flare::Expected<CompilationOutput, DistributedTaskDispatcher::ServantWaitStatus>
DistributedTaskDispatcher::WaitServantForTask(
    std::uint64_t servant_task_id, cloud::DaemonService_SyncStub* from) {
  flare::RpcClientController ctlr;

  cloud::WaitForCompilationOutputRequest req;
  req.set_token(config_keeper_.GetServingDaemonToken());
  req.set_task_id(servant_task_id);
  req.set_milliseconds_to_wait(2s / 1ms);
  req.add_acceptable_compression_algorithms(
      cloud::COMPRESSION_ALGORITHM_ZSTD);  // Hardcoded to Zstd.
  ctlr.SetTimeout(30s);
  auto result = from->WaitForCompilationOutput(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING_EVERY_SECOND("Failed to wait on task: {}",
                                   result.error().ToString());
    return ServantWaitStatus::RpcError;
  }
  if (result->status() == cloud::COMPILATION_TASK_STATUS_RUNNING) {
    // Keep waiting then.
    return ServantWaitStatus::Running;
  } else if (result->status() != cloud::COMPILATION_TASK_STATUS_DONE) {
    FLARE_LOG_ERROR_EVERY_SECOND("Unexpected task status [{}]",
                                 result->status());
    return ServantWaitStatus::Failed;
  }
  FLARE_CHECK_EQ(result->status(), cloud::COMPILATION_TASK_STATUS_DONE);

  // The task has finished (either successfully or with and error.).
  return CompilationOutput{
      .exit_code = result->exit_code(),
      .standard_output = result->output(),
      .standard_error = result->error(),
      .object_file = ctlr.GetResponseAttachment(),
  };
}

Json::Value DistributedTaskDispatcher::DumpInternals() {
  static const std::unordered_map<TaskState,
                                  std::pair<std::string, std::string>>
      kStateMapping = {
          {TaskState::Pending, {"pending_tasks", "PENDING"}},
          {TaskState::ReadyToFire, {"ready_tasks", "READY TO FIRE"}},
          {TaskState::Dispatched, {"dispatched_tasks", "DISPATCHED"}},
          {TaskState::Done, {"completed_tasks", "DONE"}},
      };
  std::scoped_lock _(tasks_lock_);
  Json::Value jsv;

  for (auto&& [k, v] : tasks_) {
    std::scoped_lock _(v->lock);

    auto&& [dir, state] = kStateMapping.at(v->state);
    auto&& entry = jsv[dir][std::to_string(k)];

    // Common parts first.
    entry["state"] = state;
    entry["requestor_pid"] = static_cast<Json::UInt64>(v->task.requestor_pid);
    entry["source_digest"] = v->task.source_digest;
    entry["compiler_digest"] = v->task.env_desc.compiler_digest();
    entry["source_path"] = v->task.source_path;
    entry["invocation_arguments"] = v->task.invocation_arguments;
    // TODO(luobogao): Write our own hacky `EnumerationToName(...)` and use it.
    entry["cache_control"] = static_cast<int>(v->task.cache_control);
    entry["preprocessed_source_size"] =
        static_cast<Json::UInt64>(v->task.preprocessed_source.ByteSize());
    entry["task_grant_id"] = static_cast<Json::UInt64>(v->task_grant_id);

    switch (v->state) {
      case TaskState::Done:
        entry["completed_at"] = FormatTime(v->completed_at);
        entry["exit_code"] = v->output.exit_code;
        entry["stdout_size"] =
            static_cast<Json::UInt64>(v->output.standard_output.size());
        entry["stderr_size"] =
            static_cast<Json::UInt64>(v->output.standard_error.size());
        entry["object_file_size"] =
            static_cast<Json::UInt64>(v->output.object_file.ByteSize());
        [[fallthrough]];

      case TaskState::Dispatched:
        entry["last_keep_alive_at"] = FormatTime(v->last_keep_alive_at);
        entry["dispatched_at"] = FormatTime(v->dispatched_at);
        entry["servant_task_id"] =
            static_cast<Json::UInt64>(v->servant_task_id);
        [[fallthrough]];

      case TaskState::ReadyToFire:
        entry["ready_at"] = FormatTime(v->ready_at);
        entry["servant_location"] = v->servant_location;
        [[fallthrough]];

      case TaskState::Pending:
        entry["start_deadline"] = FormatTime(v->start_deadline);
    }
  }
  return jsv;
}

void DistributedTaskDispatcher::OnAbortTimer() {
  std::size_t aborted = 0;

  {
    auto now = flare::ReadCoarseSteadyClock();
    std::scoped_lock _(tasks_lock_);
    for (auto&& [_, v] : tasks_) {
      if (v->start_deadline < now) {
        // `PerformOneTask` will abort the task as soon as it sees this flag.
        v->aborted.store(true, std::memory_order_relaxed);
        ++aborted;
      }
    }
  }

  FLARE_LOG_WARNING_IF(aborted,
                       "Aborted [{}] tasks, they've been in pending state "
                       "without having a chance for dispatching for too long.",
                       aborted);
}

void DistributedTaskDispatcher::OnKeepAliveTimer() {
  auto now = flare::ReadCoarseSteadyClock();
  scheduler::KeepTaskAliveRequest req;
  std::vector<std::uint64_t> task_ids;
  flare::RpcClientController ctlr;

  req.set_token(FLAGS_token);
  {
    std::scoped_lock _(tasks_lock_);
    for (auto&& [k, v] : tasks_) {
      std::scoped_lock _(v->lock);
      if (v->state != TaskState::ReadyToFire &&
          v->state != TaskState::Dispatched) {
        // The task is either still pending, or has done. No task to keep aliven
        // then.
        continue;
      }
      if (v->aborted.load(std::memory_order_relaxed)) {
        continue;
      }

      if (now - v->last_keep_alive_at > 1min) {
        // The task is likely killed by the scheduler. Abort it then..
        v->aborted.store(true, std::memory_order_relaxed);
        FLARE_LOG_WARNING_EVERY_SECOND(
            "Keep-alive timer of task {} was delayed for more than 1min, "
            "Aborting.",
            v->task_id);
        continue;
      }
      if (now - v->last_keep_alive_at > 5s) {
        FLARE_LOG_WARNING_EVERY_SECOND(
            "Our keep-alive timer is delayed for more than {} milliseconds. "
            "Overloaded?",
            (now - v->last_keep_alive_at) / 1ms);
      }
      req.add_task_grant_ids(v->task_grant_id);
      task_ids.push_back(v->task_id);
    }
  }
  req.set_next_keep_alive_in_ms(10s / 1ms);
  if (req.task_grant_ids().empty()) {
    return;  // Nothing to do then.
  }

  ctlr.SetTimeout(5s);
  auto result = scheduler_stub_.KeepTaskAlive(req, &ctlr);
  if (!result || result->statuses().size() != req.task_grant_ids().size()) {
    FLARE_LOG_WARNING(
        "Failed to send keep alive to the scheduler. We'll retry later.");
    // So be it.
  } else {
    FLARE_CHECK_EQ(task_ids.size(), req.task_grant_ids().size());

    // Now renew the last keep alive timer.
    std::scoped_lock _(tasks_lock_);
    for (int i = 0; i != task_ids.size(); ++i) {
      if (result->statuses(i)) {
        if (auto iter = tasks_.find(task_ids[i]); iter != tasks_.end()) {
          std::scoped_lock _(iter->second->lock);
          iter->second->last_keep_alive_at = now;
        } else {
          // The task has completed by the time keep-alive was sent? So be it.
        }
      } else {
        // TODO(luobogao): How to handle this error?
        FLARE_LOG_WARNING("Keep-alive request for task {} failed.",
                          task_ids[i]);
      }
    }
  }
}

void DistributedTaskDispatcher::OnKillOrphanTimer() {
  std::size_t aborted = 0;

  {
    auto now = flare::ReadCoarseSteadyClock();
    std::scoped_lock _(tasks_lock_);
    for (auto&& [_, v] : tasks_) {
      if (!v->aborted.load(std::memory_order_relaxed) &&
          !IsProcessAlive(v->task.requestor_pid)) {
        // `PerformOneTask` will abort the task as soon as it sees this flag.
        v->aborted.store(true, std::memory_order_relaxed);
        ++aborted;
      }
    }
  }

  FLARE_LOG_WARNING_IF(
      aborted, "Killed {} orphan tasks. Submitter of these tasks have gone.",
      aborted);

  // These task's descriptor will be removed by `OnCleanupTimer` some time
  // later.
}

void DistributedTaskDispatcher::OnCleanupTimer() {
  std::vector<flare::RefPtr<TaskDesc>> destroying;
  auto now = flare::ReadCoarseSteadyClock();

  {
    std::scoped_lock _(tasks_lock_);
    for (auto iter = tasks_.begin(); iter != tasks_.end();) {
      std::scoped_lock _(iter->second->lock);
      if (iter->second->state != TaskState::Done) {
        // `completed_at` is not defined in this case.
        ++iter;
        continue;
      }
      if (iter->second->aborted.load(std::memory_order_relaxed)) {
        FLARE_LOG_WARNING("Task [{}] is aborted", iter->second->task_id);

        // Keep it alive as we're holding a lock on it.
        destroying.push_back(std::move(iter->second));
        iter = tasks_.erase(iter);
        continue;
      }
      if (iter->second->completed_at + 1min < now) {
        FLARE_LOG_WARNING(
            "Task [{}] has completed for a while ({} seconds) and it seems "
            "that no one is "
            "interested in it. Dropping.",
            (now - iter->second->completed_at) / 1s, iter->second->task_id);

        // Keep it alive as we're holding a lock on it.
        destroying.push_back(std::move(iter->second));
        iter = tasks_.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  // `destroying` destroyed.
}

DistributedTaskDispatcher::TaskGrantKeeper::TaskGrantKeeper()
    : scheduler_stub_(FLAGS_scheduler_uri) {}

std::optional<DistributedTaskDispatcher::GrantDesc>
DistributedTaskDispatcher::TaskGrantKeeper::Get(
    const EnvironmentDesc& desc, const std::chrono::nanoseconds& timeout) {
  PerEnvGrantKeeper* keeper;
  {
    std::scoped_lock _(lock_);
    auto&& e = keepers_[desc.compiler_digest()];
    if (!e) {
      e = std::make_unique<PerEnvGrantKeeper>();
      e->env_desc = desc;
      e->fetcher =
          flare::Fiber([this, env = e.get()] { GrantFetcherProc(env); });
    }
    keeper = e.get();
  }

  std::unique_lock lk(keeper->lock);
  // Drop expired grants first.
  while (!keeper->remaining.empty() &&
         // We don't compensate for network delay here. We've already done that
         // in `GrantFetcherProc`.
         keeper->remaining.front().expires_at <
             flare::ReadCoarseSteadyClock()) {
    keeper->remaining.pop();
  }

  // We still have some. Satisfied without incur an RPC.
  if (!keeper->remaining.empty()) {
    auto result = keeper->remaining.front();
    keeper->remaining.pop();
    return result;
  }

  ++keeper->waiters;
  flare::ScopedDeferred _([&] { FLARE_CHECK_GE(--keeper->waiters, 0); });

  keeper->need_more_cv.notify_all();
  if (!keeper->available_cv.wait_for(
          lk, timeout, [&] { return !keeper->remaining.empty(); })) {
    return {};
  }
  auto result = keeper->remaining.front();
  keeper->remaining.pop();
  return result;
}

void DistributedTaskDispatcher::TaskGrantKeeper::Free(std::uint64_t grant_id) {
  struct Context {
    scheduler::FreeTaskRequest req;
    flare::RpcClientController ctlr;
  };

  auto ctx = std::make_shared<Context>();
  ctx->req.set_token(FLAGS_token);
  ctx->req.add_task_grant_ids(grant_id);
  ctx->ctlr.SetTimeout(5s);

  // Done asynchronously, the result is discard. Failure doesn't harm.
  scheduler_stub_.FreeTask(ctx->req, &ctx->ctlr)
      .Then([ctx = ctx, grant_id](auto result) {
        FLARE_LOG_WARNING_IF(
            !result, "Failed to free task grant [{}]. Ignoring", grant_id);
      });
}

void DistributedTaskDispatcher::TaskGrantKeeper::Stop() {
  std::scoped_lock _(lock_);
  leaving_.store(true, std::memory_order_relaxed);
  for (auto&& [_, v] : keepers_) {
    v->need_more_cv.notify_all();
  }
}

void DistributedTaskDispatcher::TaskGrantKeeper::Join() {
  // Locking should not be necessary here as not one else could have been able
  // to add new elements to the map once `Stop()` finishes.
  for (auto&& [_, v] : keepers_) {
    v->fetcher.join();
  }
}

void DistributedTaskDispatcher::TaskGrantKeeper::GrantFetcherProc(
    PerEnvGrantKeeper* keeper) {
  constexpr auto kMaxWait = 5s;
  // Tolerance of possible network delay.
  constexpr auto kNetworkDelayTolerance = 5s;
  constexpr auto kExpiresIn = 15s;

  static_assert(kExpiresIn > kMaxWait + kNetworkDelayTolerance + 1s,
                "Otherwise the grant can possibly expire immediately after RPC "
                "finishes..");

  while (!leaving_.load(std::memory_order_relaxed)) {
    std::unique_lock lk(keeper->lock);
    keeper->need_more_cv.wait(lk, [&] {
      return leaving_.load(std::memory_order_relaxed) ||
             keeper->remaining.empty();
    });
    if (leaving_.load(std::memory_order_relaxed)) {
      break;
    }

    auto before_rpc_now = flare::ReadCoarseSteadyClock();
    scheduler::WaitForStartingTaskRequest req;
    flare::RpcClientController ctlr;

    req.set_token(FLAGS_token);
    req.set_milliseconds_to_wait(kMaxWait / 1ms);
    req.set_next_keep_alive_in_ms(kExpiresIn / 1ms);
    *req.mutable_env_desc() = keeper->env_desc;
    req.set_immediate_reqs(keeper->waiters);
    req.set_prefetch_reqs(1);
    ctlr.SetTimeout(kMaxWait + 5s);

    // We don't want to hold lock during RPC.
    lk.unlock();
    auto result = flare::fiber::BlockingGet(
        scheduler_stub_.WaitForStartingTask(req, &ctlr));
    lk.lock();
    if (result) {
      // Per method definition the scheduler is not required to wait until all
      // desired grants are available. Instead, the scheduler is permitted to
      // satisfy part of our requests. So don't assume the size of the result
      // array.
      for (int i = 0; i != result->grants().size(); ++i) {
        keeper->remaining.push(GrantDesc{
            // Using timestamp prior to RPC issue, let's be conservative.
            .expires_at = before_rpc_now + kExpiresIn - kNetworkDelayTolerance,
            .grant_id = result->grants(i).task_grant_id(),
            .servant_location = result->grants(i).servant_location()});
      }
      keeper->available_cv.notify_all();
    } else {
      if (result.error().code() != scheduler::STATUS_NO_QUOTA_AVAILABLE ||
          req.immediate_reqs()) {
        FLARE_LOG_WARNING("Failed to acquire grant for starting new task: {}",
                          result.error().ToString());
      } else {
        FLARE_VLOG(1,
                   "Unable to prefetch grant for possible new-coming task. The "
                   "cloud is busy.");
      }
      // Sleep for a while before retry if we fail.
      flare::this_fiber::SleepFor(100ms);
      // Retry then, hopefully now we fetched more grants to start new tasks.
    }
  }
}

DistributedTaskDispatcher::ConfigKeeper::ConfigKeeper()
    : scheduler_stub_(FLAGS_scheduler_uri) {}

void DistributedTaskDispatcher::ConfigKeeper::Start() {
  OnFetchConfig();
  config_fetcher_ = flare::fiber::SetTimer(10s, [this] { OnFetchConfig(); });
}

void DistributedTaskDispatcher::ConfigKeeper::Stop() {
  flare::fiber::KillTimer(config_fetcher_);
}

void DistributedTaskDispatcher::ConfigKeeper::Join() {
  // NOTHING.
}

void DistributedTaskDispatcher::ConfigKeeper::OnFetchConfig() {
  scheduler::GetConfigRequest req;
  req.set_token(FLAGS_token);

  flare::RpcClientController ctlr;
  auto result = scheduler_stub_.GetConfig(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING("Failed to fetch config from scheduler.");
    return;
  }

  std::scoped_lock _(lock_);
  serving_daemon_token_ = result->serving_daemon_token();
}

}  // namespace yadcc::daemon::local
