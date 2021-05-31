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

#include "yadcc/scheduler/task_dispatcher.h"

#include <time.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <vector>

#include "thirdparty/gflags/gflags.h"

#include "flare/base/internal/time_view.h"
#include "flare/base/logging.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"

#include "yadcc/common/parse_size.h"

DEFINE_string(servant_min_memory_for_accepting_new_task, "10G",
              "If memory avaiable is less than "
              "`servant_min_memory_for_accepting_new_task`, "
              "servant will be excluded when dispatching.");

using namespace std::literals;

namespace yadcc::scheduler {

namespace {

std::string FormatTime(const flare::internal::SystemClockView& view) {
  auto time = std::chrono::system_clock::to_time_t(view.Get());
  struct tm buf;
  FLARE_CHECK(localtime_r(&time, &buf));
  char buffer[256];
  FLARE_CHECK(strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &buf));
  return buffer;
}

bool ContainsEnvironmentSlow(const std::vector<EnvironmentDesc>& envs,
                             const EnvironmentDesc& looking_for) {
  for (auto&& e : envs) {
    if (e.compiler_digest() == looking_for.compiler_digest()) {
      return true;
    }
  }
  return false;
}

// Dirty-and-quick test if `ip_port` and `ip2` points to the same host.
bool IsNetworkAddressEqual(const std::string& ip_port, const std::string& ip2) {
  return ip_port.size() > ip2.size() && ip_port[ip2.size()] == ':' &&
         flare::StartsWith(ip_port, ip2);
}

}  // namespace

TaskDispatcher* TaskDispatcher::Instance() {
  static flare::NeverDestroyed<TaskDispatcher> dispatcher;
  return dispatcher.Get();
}

TaskDispatcher::TaskDispatcher()
    : internal_exposer_("yadcc/task_dispatcher",
                        [this] { return DumpInternals(); }) {
  expiration_timer_ = flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), 1s,
                                             [this] { OnExpirationTimer(); });
  auto memory_demand =
      TryParseSize(FLAGS_servant_min_memory_for_accepting_new_task);
  FLARE_CHECK(memory_demand);
  min_memory_for_new_task_ = *memory_demand;
}

TaskDispatcher::~TaskDispatcher() {
  flare::fiber::KillTimer(expiration_timer_);
}

std::optional<TaskAllocation> TaskDispatcher::WaitForStartingNewTask(
    const TaskPersonality& personality, std::chrono::nanoseconds expires_in,
    std::chrono::nanoseconds timeout, bool prefetching) {
  // FIXME: Maybe we should bail out immediately if the requested compiler
  // digest is not recognized. Doing this allows the user to fallback to its
  // local compiler. Otherwise the user would wait indefinitely.

  std::unique_lock lk(allocation_lock_);
  std::vector<ServantDesc*> servants_eligible;
  if (!allocation_cv_.wait_for(lk, timeout, [&] {
        servants_eligible = UnsafeEnumerateEligibleServants(personality);
        return !servants_eligible.empty();
      })) {
    return std::nullopt;
  }

  // A eligible servant is available.
  auto pick = UnsafePickServantFor(servants_eligible, personality.requestor_ip);
  ++pick->running_tasks;
  ++pick->ever_assigned_tasks;

  // Create descriptor of the newly-started task
  auto task_id = tasks_.next_task_id.fetch_add(1, std::memory_order_relaxed);
  FLARE_CHECK_EQ(tasks_.tasks.count(task_id), 0);
  auto&& task = tasks_.tasks[task_id];
  task.task_id = task_id;
  task.personality = personality;
  task.belonging_servant = flare::RefPtr(flare::ref_ptr, pick);
  task.started_at = flare::ReadCoarseSteadyClock();
  task.expires_at = flare::ReadCoarseSteadyClock() + expires_in;
  task.is_prefetch = prefetching;

  return TaskAllocation{
      .task_id = task_id,
      .servant_location = pick->personality.observed_location};
}

bool TaskDispatcher::KeepTaskAlive(std::uint64_t task_id,
                                   std::chrono::nanoseconds new_expires_in) {
  std::scoped_lock _(allocation_lock_);
  auto iter = tasks_.tasks.find(task_id);
  if (iter == tasks_.tasks.end()) {
    // FIXME: This warning can be spurious. It a daemon calls both `FreeTask`
    // and `KeepTaskAlive` simultaneously, requests reordering can trigger this
    // warning falsely.
    FLARE_LOG_WARNING_EVERY_SECOND("Unexpected: Renewing unknown task [{}].",
                                   task_id);
    return false;
  }
  if (iter->second.zombie) {
    FLARE_LOG_WARNING_EVERY_SECOND(
        "The client tries to keep zombie [{}] alive. It's too late. The task "
        "was started {} seconds ago, and has already expired {} seconds ago. ",
        task_id,
        (flare::ReadCoarseSteadyClock() - iter->second.started_at) / 1s,
        (flare::ReadCoarseSteadyClock() - iter->second.expires_at) / 1s);
    return false;
  }
  iter->second.expires_at = flare::ReadCoarseSteadyClock() + new_expires_in;
  return true;
}

void TaskDispatcher::FreeTask(std::uint64_t task_id) {
  std::scoped_lock _(allocation_lock_);
  UnsafeFreeTasks({task_id});
}

void TaskDispatcher::UnsafeFreeTasks(
    const std::vector<std::uint64_t>& task_ids) {
  for (auto&& e : task_ids) {
    auto iter = tasks_.tasks.find(e);
    if (iter == tasks_.tasks.end()) {
      FLARE_LOG_WARNING_EVERY_SECOND("Unexpected: Freeing unknown task [{}].",
                                     e);
      return;
    }
    --iter->second.belonging_servant->running_tasks;
    FLARE_CHECK_EQ(tasks_.tasks.erase(e), 1);
  }

  // And wake any waiter up. Note that we must wake up all waiters as not all
  // waiters are equal.
  allocation_cv_.notify_all();
}

void TaskDispatcher::KeepServantAlive(const ServantPersonality& servant,
                                      std::chrono::nanoseconds expires_in) {
  std::scoped_lock _(allocation_lock_);

  // Let's see if we're renewing an existing servant.
  for (auto&& e : servants_.servants) {
    if (e->personality.observed_location == servant.observed_location) {
      // Had anything changed, respect whatever reported by the servant.
      e->personality = servant;
      e->expires_at = flare::ReadCoarseSteadyClock() + expires_in;
      return;
    }
  }

  // New servant then.
  auto&& added =
      servants_.servants.emplace_back(flare::MakeRefCounted<ServantDesc>());
  added->personality = servant;
  added->discovered_at = flare::ReadCoarseSteadyClock();
  added->expires_at = flare::ReadCoarseSteadyClock() + expires_in;
  added->running_tasks = 0;
  if (servant.observed_location != servant.reported_location) {
    FLARE_LOG_INFO(
        "Discovered new servant at [{}]. The servant is reporting itself at "
        "[{}]. It's likely the servant is behind NAT.",
        servant.observed_location, servant.reported_location);
  } else {
    FLARE_LOG_INFO("Discovered new servant at [{}].",
                   servant.observed_location);
  }
}

std::vector<std::uint64_t> TaskDispatcher::ExamineRunningTasks(
    const std::string& servant_location,
    const std::vector<std::uint64_t>& running_tasks) {
  std::scoped_lock _(allocation_lock_);

  // Find the servant's descriptor first.
  ServantDesc* servant = nullptr;
  for (auto&& e : servants_.servants) {
    if (e->personality.observed_location == servant_location) {
      servant = e.Get();
      break;
    }
  }

  // The servant itself has expired?
  if (!servant) {
    return running_tasks;
  }

  // For any tasks marked as zombie and not recognized by the servant, they're
  // done.
  //
  // Note that we don't care if the task is made zombie before or after this
  // keep-alive is sent by the servant. Unless there's a huge lag on the network
  // (more than task expiration), so long as the servant doesn't recognize the
  // task, we're safe. (Otherwise we may overschedule tasks to the node and
  // result in task rejection. This, in turn, should be handled by the client
  // itself.).
  UnsafeSweepZombiesOf(servant, {running_tasks.begin(), running_tasks.end()});

  // Any tasks reported by the servant but unknown to us should be returned.
  std::unordered_set<std::uint64_t> permitted_tasks;
  for (auto&& [k, v] : tasks_.tasks) {
    if (v.belonging_servant.Get() == servant && !v.zombie) {
      permitted_tasks.insert(k);
    }
  }
  std::vector<std::uint64_t> unknown_tasks;
  for (auto&& e : running_tasks) {
    if (!permitted_tasks.count(e)) {
      unknown_tasks.push_back(e);
      FLARE_VLOG(1, "Servant [{}] reported a unknown task [{}].",
                 servant->personality.observed_location, e);
    }
  }
  return unknown_tasks;
}

std::size_t TaskDispatcher::GetCapacityAvailable(
    const ServantDesc& servant_desc) const noexcept {
  if (servant_desc.personality.memory_available_in_bytes <
      min_memory_for_new_task_) {
    // Due to low memory condition, no new task can be acceptable. Therefore we
    // mark its capacity as the number of running tasks to prevent more tasks to
    // be dispatched to it.
    return servant_desc.running_tasks;
  }

  // FIXME: This is not very accurate.
  //
  // If we've just finished a heavy compilation task, `foreign_load` may be
  // spuriously larger than actual.
  //
  // This is due to sampling delay on `personality.current_load`.
  //
  // Once the compilation finishes, `running_tasks` drops instantly. Yet due to
  // sampling delay on `current_load`, some (or all) of `current_load` that
  // covers compilation jobs that have just completed, cannot be correctly
  // compensated by subtracting `running_tasks` (which by now has reached zero).
  //
  // This shouldn't hurt much, but it does reduce machine utilization when we're
  // heavy loaded.
  auto foreign_load = std::max<std::int64_t>(
      servant_desc.personality.current_load - servant_desc.running_tasks, 0);
  std::size_t capacity_available = std::max<std::int64_t>(
      servant_desc.personality.num_processors - foreign_load, 0);
  return std::min(servant_desc.personality.max_tasks, capacity_available);
}

// Allocation lock is held by the caller.
std::vector<TaskDispatcher::ServantDesc*>
TaskDispatcher::UnsafeEnumerateEligibleServants(
    const TaskPersonality& requesting_task) {
  // TODO(luobogao): Debugging code, we should turn it to an error code back to
  // the caller (RPC caller.).
  bool env_recognized = false;
  std::vector<ServantDesc*> eligibles;

  for (auto& e : servants_.servants) {
    // Requested environment not available.
    if (!ContainsEnvironmentSlow(e->personality.environments,
                                 requesting_task.env_desc)) {
      continue;
    }
    env_recognized = true;

    // Task allocated can be greater than max allowed. This happens when the
    // servant decreases its capacity after we've made some allocation (more
    // than its new capacity).
    if (e->running_tasks >= GetCapacityAvailable(*e)) {
      continue;
    }
    eligibles.push_back(e.Get());
  }
  FLARE_LOG_ERROR_IF_EVERY_SECOND(
      !env_recognized,
      "Unrecognized compilation environment [{}] is requested by [{}].",
      requesting_task.env_desc.compiler_digest(), requesting_task.requestor_ip);
  return eligibles;
}

TaskDispatcher::ServantDesc* TaskDispatcher::UnsafePickServantFor(
    std::vector<ServantDesc*> eligibles, const std::string& requestor) {
  // TODO(luobogao): I think we'd better assign a servant that the requestor has
  // been using. This allows it to reuse TCP connection (avoid slow-start after
  // idle), batch RPCs, etc. (But be care of not to use SMTs unless there are no
  // other idle servant. Besides, do not overload a single machine to much so as
  // not to block our daemon.).

  // We prefer not to assign requestor's task to itself. This should leave more
  // resource to it for "non-distributable" work such as preprocessing.
  ServantDesc* self = nullptr;
  auto iter = std::find_if(eligibles.begin(), eligibles.end(), [&](auto&& e) {
    return IsNetworkAddressEqual(e->personality.observed_location, requestor);
  });
  if (iter != eligibles.end()) {
    self = *iter;
    eligibles.erase(iter);
  }

  // If we can use a dedicated servant. Prefer it.
  if (auto ptr = UnsafeTryPickDedicatedServantFor(eligibles)) {
    return ptr;
  }

  // Otherwise let's see if we can use a servant other than the requestor
  // itself.
  if (auto ptr = UnsafeTryPickAvailableServantFor(eligibles)) {
    return ptr;
  }

  // The requestor itself must be available for handling (its own) task then,
  // otherwise we shouldn't be called.
  FLARE_CHECK(self);
  FLARE_CHECK_NE(GetCapacityAvailable(*self), 0);
  return self;
}

TaskDispatcher::ServantDesc* TaskDispatcher::UnsafeTryPickDedicatedServantFor(
    const std::vector<ServantDesc*>& eligibles) {
  // If there's a dedicated servant who hasn't reach 50% load, use it.
  //
  // FIXME: The 50% heuristic only applies if (2-way) SMT is enabled. We should
  // report whether the servant enables SMT (and if yes, how many ways of SMTs
  // are enabled), and refine the 50% check accordingly.
  return UnsafeTryPickServantFor(eligibles, [](auto&& e) {
    return e.personality.priority == SERVANT_PRIORITY_DEDICATED &&
           e.running_tasks * 2 < e.personality.num_processors;
  });
}

TaskDispatcher::ServantDesc* TaskDispatcher::UnsafeTryPickAvailableServantFor(
    const std::vector<ServantDesc*>& eligibles) {
  return UnsafeTryPickServantFor(eligibles, [](auto&&) { return true; });
}

template <class F>
TaskDispatcher::ServantDesc* TaskDispatcher::UnsafeTryPickServantFor(
    const std::vector<ServantDesc*>& eligibles, F&& pred) {
  if (eligibles.empty()) {
    return nullptr;
  }

  ServantDesc* result = nullptr;
  double min_utilization = std::numeric_limits<double>::max();

  for (auto&& e : eligibles) {
    // This can't change unless the lock was released during enumeration and
    // pick.
    FLARE_CHECK_GT(e->personality.max_tasks, e->running_tasks);

    // These servants should be given to us in the first place.
    FLARE_CHECK_NE(e->personality.max_tasks, 0);
    FLARE_CHECK_NE(GetCapacityAvailable(*e), 0);

    if (!pred(*e)) {
      continue;
    }

    double utilization =
        static_cast<double>(e->running_tasks) / GetCapacityAvailable(*e);

    // Use the servant with lowest utilization.
    if (!result || utilization < min_utilization) {
      min_utilization = utilization;
      result = e;
    }
  }

  return result;
}

void TaskDispatcher::UnsafeSweepZombiesOf(
    const ServantDesc* servant,
    const std::unordered_set<std::uint64_t>& running_tasks) {
  std::size_t non_prefetch_zombies = 0;
  std::vector<std::uint64_t> sweeping;

  // This can be slow if there are many concurrent tasks.
  for (auto&& iter = tasks_.tasks.begin(); iter != tasks_.tasks.end(); ++iter) {
    if (iter->second.belonging_servant.Get() == servant &&
        iter->second.zombie && running_tasks.count(iter->first) == 0) {
      sweeping.push_back(iter->first);
      non_prefetch_zombies += !iter->second.is_prefetch;
    }
  }

  // TODO(luobogao): For tasks started as "prefetching", we might want to
  // suppress this warning. For prefetched tasks, dropping them is a daily
  // basis.
  FLARE_LOG_WARNING_IF(non_prefetch_zombies,
                       "Sweeping {} (non-prefetched) zombie tasks.",
                       non_prefetch_zombies);
  FLARE_VLOG(10, "Sweeping {} prefetched-but-not-used zombie tasks.");
  UnsafeFreeTasks(sweeping);
}

void TaskDispatcher::UnsafeSweepOrphans() {
  std::vector<std::uint64_t> sweeping;

  std::unordered_set<ServantDesc*> alive_servants;
  for (auto&& e : servants_.servants) {
    alive_servants.insert(e.Get());
  }

  // This can be slow if there are many concurrent tasks.
  for (auto&& iter = tasks_.tasks.begin(); iter != tasks_.tasks.end(); ++iter) {
    if (alive_servants.count(iter->second.belonging_servant.Get()) == 0) {
      sweeping.push_back(iter->first);
    }
  }

  FLARE_LOG_WARNING_IF(!sweeping.empty(), "Sweeping {} orphan tasks.",
                       sweeping.size());
  UnsafeFreeTasks(sweeping);
}

void TaskDispatcher::OnExpirationTimer() {
  auto now = flare::ReadCoarseSteadyClock();
  std::scoped_lock _(allocation_lock_);

  // Remove expired servants.
  for (auto iter = servants_.servants.begin();
       iter != servants_.servants.end();) {
    if ((*iter)->expires_at < now) {
      FLARE_LOG_INFO(
          "Removing expired servant [{}]. It served us for {} seconds.",
          (*iter)->personality.observed_location,
          (flare::ReadCoarseSteadyClock() - (*iter)->discovered_at) / 1s);
      iter = servants_.servants.erase(iter);
    } else {
      ++iter;
    }
  }

  // Immediately forget (without making them zombie) about tasks whose servant
  // has gone.
  UnsafeSweepOrphans();

  // Make expired tasks zombie.
  for (auto iter = tasks_.tasks.begin(); iter != tasks_.tasks.end(); ++iter) {
    if (iter->second.expires_at < now) {
      iter->second.zombie = true;
      FLARE_VLOG(1,
                 "Task [{}] expired {} milliseconds ago. It has been there for "
                 "{} seconds.{}",
                 iter->first, (now - iter->second.expires_at) / 1ms,
                 (now - iter->second.started_at) / 1s,
                 iter->second.is_prefetch
                     ? " The task was started because of a prefetch request."
                     : "");
    }
  }
}

Json::Value TaskDispatcher::DumpInternals() {
  std::scoped_lock _(allocation_lock_);
  Json::Value jsv;
  std::uint64_t cluster_capacity = 0;
  std::uint64_t capacity_unavailable = 0;
  std::uint64_t total_running = 0;

  // Servants.
  for (int i = 0; i != servants_.servants.size(); ++i) {
    auto&& entry = servants_.servants[i];
    auto&& personality = entry->personality;
    auto&& item = jsv["servants"][i];

    item["version"] = personality.version;
    if (personality.observed_location != personality.reported_location) {
      item["observed_location"] = personality.observed_location;
      item["reported_location"] = personality.reported_location;
    } else {
      item["location"] = personality.observed_location;
    }
    item["discovered_at"] = FormatTime(entry->discovered_at);
    item["expires_at"] = FormatTime(entry->expires_at);
    for (auto&& e : personality.environments) {
      item["environments"].append(e.compiler_digest());
    }
    item["priority"] = ServantPriority_Name(personality.priority);
    if (personality.max_tasks) {
      item["max_tasks"] = static_cast<Json::UInt64>(personality.max_tasks);
    } else {
      item["not_accepting_task_reason"] =
          NotAcceptingTaskReason_Name(personality.not_accepting_task_reason);
    }
    item["num_processors"] =
        static_cast<Json::UInt64>(personality.num_processors);
    item["current_load"] = static_cast<Json::UInt64>(personality.current_load);
    item["capacity_available"] =
        static_cast<Json::Int64>(GetCapacityAvailable(*entry));
    item["running_tasks"] = static_cast<Json::UInt64>(entry->running_tasks);
    item["ever_assigned_tasks"] =
        static_cast<Json::UInt64>(entry->ever_assigned_tasks);

    total_running += entry->running_tasks;
    cluster_capacity += personality.max_tasks;
    capacity_unavailable +=
        personality.max_tasks - GetCapacityAvailable(*entry);
  }

  // Tasks.
  for (auto&& [k, v] : tasks_.tasks) {
    auto&& item = jsv["tasks"][std::to_string(k)];

    item["task_id"] = static_cast<Json::UInt64>(v.task_id);
    item["requestor_ip"] = v.personality.requestor_ip;
    item["compiler_digest"] = v.personality.env_desc.compiler_digest();
    item["started_at"] = FormatTime(v.started_at);
    item["expires_at"] = FormatTime(v.expires_at);
    item["prefetched_task"] = v.is_prefetch;
    item["servant_location"] =
        v.belonging_servant->personality.observed_location;
    item["zombie"] = v.zombie;
  }

  jsv["servants_up"] = static_cast<Json::UInt64>(servants_.servants.size());
  jsv["running_tasks"] = static_cast<Json::UInt64>(total_running);
  jsv["capacity"] = static_cast<Json::UInt64>(cluster_capacity);
  // The result of subtraction can be negative, if some node is running more
  // tasks than it's capable (e.g., if the capacity drops after the tasks were
  // started.).
  jsv["capacity_available"] = static_cast<Json::UInt64>(std::max<std::int64_t>(
      cluster_capacity - total_running - capacity_unavailable, 0));
  jsv["capacity_unavailable"] = static_cast<Json::UInt64>(capacity_unavailable);
  return jsv;
}

}  // namespace yadcc::scheduler
