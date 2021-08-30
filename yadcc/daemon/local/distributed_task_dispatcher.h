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

#ifndef YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_DISPATCHER_H_
#define YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_DISPATCHER_H_

#include <chrono>
#include <cinttypes>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "flare/base/buffer.h"
#include "flare/base/expected.h"
#include "flare/base/exposed_var.h"
#include "flare/base/ref_ptr.h"
#include "flare/base/status.h"
#include "flare/base/type_index.h"
#include "flare/fiber/condition_variable.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/latch.h"
#include "flare/fiber/mutex.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/api/scheduler.flare.pb.h"
#include "yadcc/daemon/local/config_keeper.h"
#include "yadcc/daemon/local/distributed_cache_reader.h"
#include "yadcc/daemon/local/distributed_task.h"
#include "yadcc/daemon/local/running_task_keeper.h"
#include "yadcc/daemon/local/task_grant_keeper.h"

namespace yadcc::daemon::local {

// This class accepts tasks from our HTTP handler and schedule the tasks to the
// cloud as it sees fit.
class DistributedTaskDispatcher {
 public:
  static DistributedTaskDispatcher* Instance();

  DistributedTaskDispatcher();
  ~DistributedTaskDispatcher();

  enum class WaitStatus { OK, Timeout, NotFound };

  // Schedule a task for remote execution.
  //
  // If the task can't be scheduled before `start_deadline`, it'll be aborted
  // (and `WaitForTask` will report the task as failed).
  //
  // Note that we check deadlines in a coarse time granularity, so don't expect
  // it to be precise.
  template <class T>
  std::uint64_t QueueTask(std::unique_ptr<T> task,
                          std::chrono::steady_clock::time_point start_deadline);

  // Wait for a specific task to complete.
  //
  // Note that `T` must be an exact match of type that was used for calling
  // `QueueTask`.
  template <class T>
  flare::Expected<std::unique_ptr<T>, WaitStatus> WaitForTask(
      std::uint64_t task_id, std::chrono::nanoseconds timeout);

  void Stop();
  void Join();

 private:
  FRIEND_TEST(HttpServiceImpl, Cxx);

  class TaskDesc;
  class GrantDesc;

  enum class ServantWaitStatus { Running, RpcError, Failed };

  // Submit a task. `type` will be saved and compared on waiting for the task.
  //
  // TBH `dynamic_cast` perfectly suits our needs here, yet it is discouraged by
  // our coding convention. Besides, I'm biased against it.
  std::uint64_t QueueDistributedTask(
      flare::TypeIndex type, std::unique_ptr<DistributedTask> task,
      std::chrono::steady_clock::time_point start_deadline);

  // Wait for task with the specifid type and ID.
  flare::Expected<std::unique_ptr<DistributedTask>, WaitStatus>
  WaitForDistributedTask(flare::TypeIndex type, std::uint64_t task_id,
                         std::chrono::nanoseconds timeout);

  // This method does everything necessary to complete a task.
  //
  // It's executed in its dedicated fiber.
  void PerformOneTask(flare::RefPtr<TaskDesc> task);

  // If allowed, this method use cached result to satisfy the task.
  bool TryReadCacheIfAllowed(TaskDesc* task);

  // If the same source code is being compiled elsewhere, this method reuse that
  // task.
  bool TryGetExistingTaskResult(TaskDesc* task);

  // This method submits task to a compile-server and wait for its completion.
  void StartNewServantTask(TaskDesc* task);

  // Wait on `servant` for task with ID `servant_task_id`.
  //
  // Returns `std::nullopt` if the task is still running on return.
  flare::Expected<DistributedTaskOutput, ServantWaitStatus> WaitServantForTask(
      std::uint64_t servant_task_id, cloud::DaemonService_SyncStub* from);

  void WaitServantForTaskWithRetry(TaskDesc* task,
                                   cloud::DaemonService_SyncStub* from);

  void FreeServantTask(std::uint64_t servant_task_id,
                       cloud::DaemonService_SyncStub* from);

  Json::Value DumpInternals();

 private:
  // Abort tasks that have been in pending queue for too long.
  void OnAbortTimer();

  // Keep tasks that have been dispatched alive.
  void OnKeepAliveTimer();

  // Kills tasks whose submitter has gone.
  void OnKillOrphanTimer();

  // Frees tasks that has been completed for a while and no one ever read it.
  void OnCleanupTimer();

  std::optional<std::vector<std::pair<std::string, flare::NoncontiguousBuffer>>>
  TryParseFiles(const flare::NoncontiguousBuffer& bytes);

 private:
  enum class TaskState {
    // The task has not yet run.
    Pending,

    // Scheduler has granted us the permission to start the task.
    ReadyToFire,

    // The task has been dispatched to a servant.
    Dispatched,

    // The task has completed.
    Done
  };

  struct TaskDesc : public flare::RefCounted<TaskDesc> {
    ///////////////////////////////
    // NOT protected by `lock`.  //
    ///////////////////////////////

    // Immutable since creation.
    std::uint64_t task_id;
    flare::TypeIndex task_type;
    std::unique_ptr<DistributedTask> task;
    std::chrono::steady_clock::time_point start_deadline;

    // Thread-safe itself. Signaled after completion only. No consistency issue
    // possible.
    flare::fiber::Latch completion_latch{1};

    // If, for any reason, the task should be aborted, this flag is set.
    //
    // `PerformOneTask` periodically check this flag, and bail out early if
    // asked to.
    std::atomic<bool> aborted{false};

    ///////////////////////////
    // Protected by `lock`.  //
    ///////////////////////////

    flare::fiber::Mutex lock;  // Ordered later than `tasks_lock_`.

    // Execution result.
    std::chrono::steady_clock::time_point started_at{};  // Immutable since
                                                         // creation, actually.
    std::chrono::steady_clock::time_point ready_at{};
    std::chrono::steady_clock::time_point dispatched_at{};
    std::chrono::steady_clock::time_point completed_at{};
    DistributedTaskOutput output;

    ///////////////////////////////////////////////////////
    // Internal states go below. (Protected by `lock`.)  //
    ///////////////////////////////////////////////////////

    TaskState state = TaskState::Pending;
    // ID allocated by the scheduler. We need to renew it periodically until
    // task completes.
    std::uint64_t task_grant_id = 0;
    std::string servant_location;       // IP:port
    std::uint64_t servant_task_id = 0;  // Task ID allocated by the servant.
    std::chrono::steady_clock::time_point last_keep_alive_at;
  };

  scheduler::SchedulerService_SyncStub scheduler_stub_;

  std::uint64_t abort_timer_;        // Aborts tasks queued for too long.
  std::uint64_t keep_alive_timer_;   // Keeps tasks dispatched alive.
  std::uint64_t kill_orphan_timer_;  // Kills task whose submitter is dead.
  std::uint64_t cleanup_timer_;      // Drops completed tasks that no one cares.

  ConfigKeeper config_keeper_;
  TaskGrantKeeper task_grant_keeper_;
  RunningTaskKeeper running_task_keeper_;

  flare::fiber::Mutex tasks_lock_;
  std::unordered_map<std::uint64_t, flare::RefPtr<TaskDesc>> tasks_;

  std::atomic<std::uint64_t> hit_cache_{0};
  std::atomic<std::uint64_t> reuse_existing_result_{0};
  std::atomic<std::uint64_t> actually_run_{0};

  flare::ExposedVarDynamic<Json::Value> internal_exposer_;
};

template <class T>
std::uint64_t DistributedTaskDispatcher::QueueTask(
    std::unique_ptr<T> task,
    std::chrono::steady_clock::time_point start_deadline) {
  return QueueDistributedTask(flare::GetTypeIndex<T>(), std::move(task),
                              start_deadline);
}

template <class T>
flare::Expected<std::unique_ptr<T>, DistributedTaskDispatcher::WaitStatus>
DistributedTaskDispatcher::WaitForTask(std::uint64_t task_id,
                                       std::chrono::nanoseconds timeout) {
  auto result =
      WaitForDistributedTask(flare::GetTypeIndex<T>(), task_id, timeout);
  if (result) {
    // Down-casting it to the resulting type.
    //
    // Unless there's a bug in our implementation, runtime type of `result` must
    // match `T`.
    return std::unique_ptr<T>(static_cast<T*>(result->release()));
  }
  return result.error();
}

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_DISPATCHER_H_
