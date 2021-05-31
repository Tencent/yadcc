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

#ifndef YADCC_SCHEDULER_TASK_DISPATCHER_H_
#define YADCC_SCHEDULER_TASK_DISPATCHER_H_

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "thirdparty/jsoncpp/value.h"

#include "flare/base/exposed_var.h"
#include "flare/base/ref_ptr.h"
#include "flare/fiber/condition_variable.h"
#include "flare/fiber/mutex.h"

#include "yadcc/api/env_desc.pb.h"
#include "yadcc/api/scheduler.flare.pb.h"

namespace yadcc::scheduler {

// Describes several aspect of a task. The dispatcher may use these information
// to determine which node should be allocated to this task.
struct TaskPersonality {
  // For the moment we ignore this. In the future we may found it necessary to
  // avoid communication between machines belonging to different trust domain
  // for better security. (The client trusts whatever returned as "compilation
  // result" returned by the compile-server. In untrusted environment this can
  // be dangerous.)
  std::string requestor_ip;

  // We can only allocate compile-server that recognizes this environment to the
  // client. Otherwise the server we allocated will have no toolchain to serve
  // the client.
  EnvironmentDesc env_desc;

  // I'm not sure if other environment personalities should be checked. e.g.,
  // Linux (distribution, I mean) version, ISA, etc.
};

// Describes a successful task allocation.
struct TaskAllocation {
  // Task ID allocated to this task request. The user should renew this
  // allocation (by calling `KeepTaskAlive`) periodically to prevent it being
  // killed prematurely.
  std::uint64_t task_id;

  // IP:port of the servant.
  std::string servant_location;
};

// Describes a servant.
struct ServantPersonality {
  // TODO(luobogao): UUID

  // Daemon version.
  int version;

  // IP:port of the servant as observed by us.
  std::string observed_location;

  // Location reported by the servant. If this one does not match `location`
  // above, it's likely the servant is behind NAT.
  std::string reported_location;

  // Compilers available on the servant.
  std::vector<EnvironmentDesc> environments;

  // Maximum cpu core of daemon machine
  std::size_t num_processors;

  // Maximum concurrent task this servant can process.
  std::size_t max_tasks;

  // Total memory of this servant.
  std::size_t total_memory_in_bytes;

  // Available memory of this servant.
  std::size_t memory_available_in_bytes;

  // Priority of this servant.
  ServantPriority priority;

  // Reason why `max_tasks` is zero.
  NotAcceptingTaskReason not_accepting_task_reason;

  // Recent load average
  std::size_t current_load;
};

// This class is responsible for assigning compile-server to requestor. The
// assignment may depend on several factors.
class TaskDispatcher {
 public:
  static TaskDispatcher* Instance();

  TaskDispatcher();
  ~TaskDispatcher();

  ///////////////////////////////
  // Task servant allocation.  //
  ///////////////////////////////

  // Apply for starting a new task. The allocation is valid until `expires_in`
  // (i.e., "lease", if this terms is more familiar to you).
  //
  // `expires_in` is counted after the task is allowed. Time elapsed during
  // waiting is not counted.
  //
  // If no allocation can be done in `timeout`, this methods fails with
  // `std::nullopt`.
  std::optional<TaskAllocation> WaitForStartingNewTask(
      const TaskPersonality& personality, std::chrono::nanoseconds expires_in,
      std::chrono::nanoseconds timeout, bool prefetching);

  // Expands a task's allocation to `new_expires_in`.
  //
  // Returns `false` if task ID given is not recognized (e.g., already expired).
  bool KeepTaskAlive(std::uint64_t task_id,
                     std::chrono::nanoseconds new_expires_in);

  // Free a task allocation.
  //
  // Calling this method allows us to free task allocation more quickly.
  // However, calling this method is NOT required for correctness. The
  // allocation is automatically freed once it's expired.
  void FreeTask(std::uint64_t task_id);

  ////////////////////////////
  // Servant maintainance.  //
  ////////////////////////////

  // Called as a result of servant heartbeat. The dispatcher automatically
  // register this servant if the servant is new to the dispatcher. Otherwise we
  // expands servant's aliveness to `expires_in`.
  //
  // The servant is automatically declared as dead as removed if no new call to
  // this method happens before `expires_in`.
  void KeepServantAlive(const ServantPersonality& servant,
                        std::chrono::nanoseconds expires_in);

  // Examine running tasks reported by the servant. This method is called as a
  // result of servant heartbeat.
  //
  // If the dispatcher found any task(s) unknown to it, the corresponding task
  // ID(s) will be returned. The reporting servant is free (and advised) to kill
  // these unknown (presumably because of task allocation auto-expiration)
  // tasks.
  std::vector<std::uint64_t> ExamineRunningTasks(
      const std::string& servant_location,  // TODO(luobogao): Use UUID here.
      const std::vector<std::uint64_t>& running_tasks);

 private:
  struct ServantDesc : public flare::RefCounted<ServantDesc> {
    ServantPersonality personality;
    std::chrono::steady_clock::time_point discovered_at;
    std::chrono::steady_clock::time_point expires_at;

    std::size_t running_tasks = 0;
    std::size_t ever_assigned_tasks = 0;

    // Get capacity available to us (not used by other jobs on the node.).
  };

  struct ServantRegistry {
    std::vector<flare::RefPtr<ServantDesc>> servants;
  };

  struct TaskDesc {
    std::uint64_t task_id;
    TaskPersonality personality;
    flare::RefPtr<ServantDesc> belonging_servant;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point expires_at;
    bool is_prefetch;

    // We don't instantly forget about expired tasks (if this does happen).
    // Instead, we keep it as a zombie until a call to `KeepServantAlive`
    // signaling that this task is not running on the corresponding servant.
    //
    // The reason is that, if we forget about the tasks immediately, before the
    // servant is notified, we risks overloading the corresponding servant by
    // assigning new tasks to it immediately.
    bool zombie = false;
  };

  struct TaskRegistry {
    std::atomic<std::uint64_t> next_task_id{};
    std::unordered_map<std::uint64_t, TaskDesc> tasks;
  };

  // Get capacity available to us (not used by other jobs on the node.).
  std::size_t GetCapacityAvailable(
      const ServantDesc& servant_desc) const noexcept;

  void UnsafeFreeTasks(const std::vector<std::uint64_t>& task_ids);

  // Enumerates all servants eligible of handling the requesting task.
  //
  // Allocation lock must be held by the caller.
  std::vector<ServantDesc*> UnsafeEnumerateEligibleServants(
      const TaskPersonality& requesting_task);

  // Pick a servant for handling this request. The implementation may do some
  // heuristics for optimizing workload distribution.
  //
  // Allocation lock must be held by the caller. This guarantees validity of
  // pointers in `eligibles`.
  ServantDesc* UnsafePickServantFor(std::vector<ServantDesc*> eligibles,
                                    const std::string& requestor);

  // Pick a dedicated (if available) servant for the given request. If no
  // dedicated servant is idle enough for new request, this method may return
  // null.
  ServantDesc* UnsafeTryPickDedicatedServantFor(
      const std::vector<ServantDesc*>& eligibles);

  // Pick a servant for the given request.
  ServantDesc* UnsafeTryPickAvailableServantFor(
      const std::vector<ServantDesc*>& eligibles);

  // Pick a servant that satisfies the given predicate.
  template <class F>
  ServantDesc* UnsafeTryPickServantFor(
      const std::vector<ServantDesc*>& eligibles, F&& pred);

  // Forget about tasks that are marked as "zombie" and no longer recognized by
  // the corresponding servant.
  void UnsafeSweepZombiesOf(
      const ServantDesc* servant,
      const std::unordered_set<std::uint64_t>& running_tasks);

  // Forget about tasks whose servant has gone (presumably due to keep-alive
  // miss).
  void UnsafeSweepOrphans();

  // Check for task / servant expiration.
  void OnExpirationTimer();

  // Dump internal state for debugging.
  Json::Value DumpInternals();

 private:
  std::uint64_t expiration_timer_;

  // Note that the current implementation doesn't scale well. Each time a
  // servant is released, all pending allocations are waken up and check all
  // servant for eligibility, **with lock held**. This effectively serializes a
  // large amount of CPU computation, without a successful allocation guarantee.
  //
  // For the moment this doesn't matter much as we don't expect there to be too
  // many servants. If this turns out to be a bottleneck in the future, we can
  // shard internal state by, e.g., environment available of each servant.
  flare::fiber::Mutex allocation_lock_;
  flare::fiber::ConditionVariable allocation_cv_;

  // Protected by `allocation_lock_`.
  ServantRegistry servants_;
  TaskRegistry tasks_;

  // Exposes some internals for debugging.
  flare::ExposedVarDynamic<Json::Value> internal_exposer_;

  // Parsed from `FLAGS_min_memory_for_dispatching_servant` when initializing.
  std::size_t min_memory_for_new_task_;
};

}  // namespace yadcc::scheduler

#endif  // YADCC_SCHEDULER_TASK_DISPATCHER_H_
