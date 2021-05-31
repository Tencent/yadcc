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
#include <queue>
#include <string>

#include "flare/base/buffer.h"
#include "flare/base/exposed_var.h"
#include "flare/base/ref_ptr.h"
#include "flare/base/status.h"
#include "flare/fiber/condition_variable.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/latch.h"
#include "flare/fiber/mutex.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/api/scheduler.flare.pb.h"

namespace yadcc::daemon::local {

// @sa: `client/env_options.h`
enum class CacheControl {
  Disallow = 0,  // Don't touch cache.
  Allow = 1,     // Use existing one, or fill it on cache miss.
  Refill = 2  // Do not use the existing cache, but (re)fills it on completion.
};

struct CompilationTask {
  pid_t requestor_pid;
  EnvironmentDesc env_desc;
  std::string source_path;
  std::string invocation_arguments;
  CacheControl cache_control;
  // @sa: `GetCacheEntryKey` in `cache_format.h`. Not applicable if caching is
  // disabled.
  std::string source_digest;
  flare::NoncontiguousBuffer preprocessed_source;  // Zstd compressed.
};

struct CompilationOutput {
  int exit_code;
  std::string standard_output, standard_error;
  flare::NoncontiguousBuffer object_file;
};

// This class accepts tasks from our HTTP handler and schedule the tasks to the
// cloud as it sees fit.
class DistributedTaskDispatcher {
 public:
  static DistributedTaskDispatcher* Instance();

  DistributedTaskDispatcher();
  ~DistributedTaskDispatcher();

  enum WaitStatus { OK, Timeout, NotFound };

  // Schedule a compilation task.
  //
  // If the task can't be scheduled before `start_deadline`, it'll be aborted
  // (and `WaitForTask` will report the task as failed).
  //
  // Note that we check deadlines in a coarse time granularity, so don't expect
  // it be precise.
  //
  // TODO(luobogao): running_timeout?
  std::uint64_t QueueTask(CompilationTask task,
                          std::chrono::steady_clock::time_point start_deadline);

  // Wait for a specific task to complete.
  WaitStatus WaitForTask(std::uint64_t task_id,
                         std::chrono::nanoseconds timeout,
                         CompilationOutput* output);

  void Stop();
  void Join();

 private:
  class TaskDesc;
  class GrantDesc;

  enum class ServantWaitStatus { Running, RpcError, Failed };

  // This method does everything necessary to complete a task, including:
  //
  // - Looking up cache.
  // - (On cache miss) Submitting the task to the cloud.
  // - (On cache miss) Polling task state until it's completed.
  // - Signal the task to wake up waiter (if any).
  //
  // It's executed in its dedicated fiber.
  void PerformOneTask(flare::RefPtr<TaskDesc> task);

  // Submit task to the servant we've just be granted.
  //
  // Returns task ID allocated by the servant.
  flare::Expected<std::uint64_t, flare::Status> SubmitTaskToServant(
      std::uint64_t grant_id, const TaskDesc& task,
      cloud::DaemonService_SyncStub* to);

  // Wait on `servant` for task with ID `servant_task_id`.
  //
  // Returns `std::nullopt` if the task is still running on return.
  flare::Expected<CompilationOutput, ServantWaitStatus> WaitServantForTask(
      std::uint64_t servant_task_id, cloud::DaemonService_SyncStub* from);

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

 private:
  enum TaskState {
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
    CompilationTask task;
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
    CompilationOutput output;

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

  // Describes a task grant alloacted by the scheduler.
  struct GrantDesc {
    // Network delay has been compensated by the fetcher by substracting a
    // period of time from `expires_at`. Don't do that yourself again.
    std::chrono::steady_clock::time_point expires_at;
    std::uint64_t grant_id;
    std::string servant_location;

    bool operator<(const GrantDesc& other) const {
      return expires_at > other.expires_at;
    }
  };

  // This class helps us to grab, and if necessary, prefetch grants for starting
  // new tasks, from our scheduler.
  class TaskGrantKeeper {
   public:
    TaskGrantKeeper();

    // Grab a grant for starting new task.
    std::optional<GrantDesc> Get(const EnvironmentDesc& desc,
                                 const std::chrono::nanoseconds& timeout);

    // Free a previous allocated grant.
    void Free(std::uint64_t grant_id);

    void Stop();
    void Join();

   private:
    struct PerEnvGrantKeeper;

    // Wake up the fiber for fetching new grants.
    void WakeGrantFetcherFor(const EnvironmentDesc& desc);

    void GrantFetcherProc(PerEnvGrantKeeper* keeper);

   private:
    struct PerEnvGrantKeeper {
      EnvironmentDesc env_desc;  // Our environment.

      flare::fiber::Mutex lock;
      flare::fiber::ConditionVariable need_more_cv, available_cv;

      // Number of waiters waiting on us.
      int waiters = 0;

      // Available grants. They'll either be handed to `waiters`, or in case we
      // have spare ones, save here.
      //
      // Besides, if we prefetched some grants, they're saved here too.
      // Prefetching helps to reduce latency in critical path.
      std::queue<GrantDesc> remaining;

      // Fiber for fetching more grants.
      flare::Fiber fetcher;
    };

    scheduler::SchedulerService_AsyncStub scheduler_stub_;

    flare::fiber::Mutex lock_;
    std::atomic<bool> leaving_ = false;

    // We never clean up this map, in case the client keep sending us random
    // environment, this will be a DoS vulnerability.
    std::unordered_map<std::string, std::unique_ptr<PerEnvGrantKeeper>>
        keepers_;
  };

  class ConfigKeeper {
   public:
    ConfigKeeper();

    std::string GetServingDaemonToken() const {
      std::scoped_lock _(lock_);
      return serving_daemon_token_;
    }

    void Start();
    void Stop();
    void Join();

   private:
    void OnFetchConfig();

   private:
    std::uint64_t config_fetcher_;
    scheduler::SchedulerService_SyncStub scheduler_stub_;

    mutable std::mutex lock_;
    std::string serving_daemon_token_;
  };

  scheduler::SchedulerService_SyncStub scheduler_stub_;

  std::uint64_t abort_timer_;        // Aborts tasks queued for too long.
  std::uint64_t keep_alive_timer_;   // Keeps tasks dispatched alive.
  std::uint64_t kill_orphan_timer_;  // Kills task whose submitter is dead.
  std::uint64_t cleanup_timer_;      // Drops completed tasks that no one cares.

  ConfigKeeper config_keeper_;
  TaskGrantKeeper task_grant_keeper_;

  flare::fiber::Mutex tasks_lock_;
  std::unordered_map<std::uint64_t, flare::RefPtr<TaskDesc>> tasks_;

  flare::ExposedVarDynamic<Json::Value> internal_exposer_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_DISPATCHER_H_
