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

#include "yadcc/daemon/local/local_task_monitor.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "thirdparty/gflags/gflags.h"

#include "flare/base/internal/cpu.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"

using namespace std::literals;

// `max_local_tasks` defaults to `nproc` / 2. Unfortunately. In our test,
// defaulting it to `nproc` leads to OOM easily for linking-heavy workload.
DEFINE_int32(max_local_tasks, 0,
             "Maximum number of local tasks that can be started by yadcc "
             "concurrently. The default is `nproc` / 2.");
DEFINE_double(lightweight_local_task_overprovisioning_ratio, 1,
              "For lightweight local tasks we allow some overprovisioning. "
              "This option determines overprovision ratio.");

namespace yadcc::daemon::local {

namespace {

// Tests if the given process ID still exists.
bool IsProcessAlive(pid_t pid) {
  std::ifstream ifs(flare::Format("/proc/{}/status", pid));

  // `/proc/pid` is not present then.
  //
  // FIXME: What about non-`ENOENT` error?
  if (!ifs) {
    return false;
  }

  std::string str;
  while (std::getline(ifs, str)) {
    if (flare::StartsWith(str, "State:")) {
      // R  Running
      // S  Sleeping in an interruptible wait
      // D  Waiting in uninterruptible disk sleep
      // Z  Zombie
      // T  Stopped (on a signal) or (before Linux 2.6.33) trace stopped
      // t  Tracing stop (Linux 2.6.33 onward)
      // W  Paging (only before Linux 2.6.0)
      // X  Dead (from Linux 2.6.0 onward)
      // x  Dead (Linux 2.6.33 to 3.13 only)
      // K  Wakekill (Linux 2.6.33 to 3.13 only)
      // W  Waking (Linux 2.6.33 to 3.13 only)
      // P  Parked (Linux 3.9 to 3.13 only)
      std::stringstream ss;
      ss << str;
      std::string x, y;
      ss >> x >> y;
      return y != "Z" && y != "x" && y != "X";
    }
  }
  FLARE_LOG_FATAL("State of process [{}] cannot be determined.", pid);
}

// In containerized environment, `nproc` does not tell us the "real" processor
// we can use. `/sys/fs/cgroup/cpu/cpu.cfs_quota_us` *sometimes* tells us the
// "real" processor quota allocated to us.
std::optional<std::size_t> DetermineCGroupsProcessorQuota() {
  std::ifstream quotafs("/sys/fs/cgroup/cpu/cpu.cfs_quota_us"),
      periodfs("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
  std::int64_t quota, period;

  quotafs >> quota;
  periodfs >> period;
  if (!quotafs || !periodfs) {
    return std::nullopt;
  }
  if (quota <= 0 || period <= 0) {
    return std::nullopt;
  }
  return std::min<std::size_t>(
      flare::internal::GetNumberOfProcessorsAvailable(), quota / period);
}

}  // namespace

LocalTaskMonitor* LocalTaskMonitor::Instance() {
  static flare::NeverDestroyed<LocalTaskMonitor> monitor;
  return monitor.Get();
}

LocalTaskMonitor::LocalTaskMonitor()
    : internal_exposer_("yadcc/local_task_mgr",
                        [this] { return DumpInternals(); }) {
  if (FLAGS_max_local_tasks) {
    max_tasks_ = FLAGS_max_local_tasks;
  } else {
    if (auto cgroup_quota = DetermineCGroupsProcessorQuota()) {
      FLARE_LOG_INFO("CGroups present, {} processors are allocated to us.",
                     *cgroup_quota);
      max_tasks_ = *cgroup_quota / 2;
    } else {
      max_tasks_ = flare::internal::GetNumberOfProcessorsAvailable() / 2;
    }
  }
  max_tasks_ = std::max<std::size_t>(max_tasks_, 1);
  lightweight_task_overprovisioning_ =
      max_tasks_ * FLAGS_lightweight_local_task_overprovisioning_ratio;

  alive_process_check_timer_ = flare::fiber::SetTimer(
      flare::ReadCoarseSteadyClock(), 1s, [this] { OnAliveProcessCheck(); });
}

LocalTaskMonitor::~LocalTaskMonitor() {
  flare::fiber::KillTimer(alive_process_check_timer_);
}

bool LocalTaskMonitor::WaitForRunningNewTaskPermission(
    pid_t starting_task_pid, bool lightweight,
    std::chrono::nanoseconds timeout) {
  // Exposed via `DumpInternals()`.
  auto&& waiter_counter =
      lightweight ? &lightweight_waiters_ : &heavyweight_waiters_;
  flare::ScopedDeferred _{
      [&] { waiter_counter->fetch_sub(1, std::memory_order_relaxed); }};
  waiter_counter->fetch_add(1, std::memory_order_relaxed);

  // Now wait for permission to start new task.
  std::unique_lock lk(permission_lock_);
  auto success = permission_cv_.wait_for(lk, timeout, [&] {
    return permissions_granted_.size() <
           max_tasks_ + (lightweight ? lightweight_task_overprovisioning_ : 0);
  });

  if (success) {
    if (permissions_granted_.count(starting_task_pid)) {
      FLARE_LOG_ERROR_EVERY_SECOND(
          "Unexpected: Duplicated process ID [{}]. Allowing this task blindly.",
          starting_task_pid);
      // `current_tasks_` is NOT incremented. This request is ignored.
      return true;
    }
    // Remember process ID of the task.
    permissions_granted_.insert(starting_task_pid);
  }
  return success;
}

void LocalTaskMonitor::DropTaskPermission(pid_t pid) {
  {
    std::scoped_lock _(permission_lock_);
    auto erased = permissions_granted_.erase(pid);
    if (erased == 0) {
      FLARE_LOG_ERROR_EVERY_SECOND(
          "Unexpected: Dropping permission of unknown task with process ID "
          "[{}].",
          pid);
      return;  // Ignored then.
    }
    FLARE_CHECK_EQ(erased, 1);
  }

  // Do NOT call `notify_one` here. Not all waiters are equal. If the permission
  // count dropped below "lightweight task overprovisioning" threshold, and
  // `notify_one` wake up a heavy task requestor, we'll lost an overprovisioning
  // oppertunity.
  permission_cv_.notify_all();
}

void LocalTaskMonitor::OnAliveProcessCheck() {
  std::scoped_lock _(permission_lock_);
  for (auto iter = permissions_granted_.begin();
       iter != permissions_granted_.end();) {
    if (!IsProcessAlive(*iter)) {
      FLARE_LOG_WARNING_EVERY_SECOND(
          "Process [{}] exited without notifying us. Crashed?", *iter);
      iter = permissions_granted_.erase(iter);
    } else {
      ++iter;
    }
  }
  // Same argument as `DropTaskPermission`.
  permission_cv_.notify_all();
}

Json::Value LocalTaskMonitor::DumpInternals() {
  std::scoped_lock _(permission_lock_);
  Json::Value jsv;

  jsv["heavyweight_waiters"] = static_cast<Json::UInt64>(
      heavyweight_waiters_.load(std::memory_order_relaxed));
  jsv["lightweight_waiters"] = static_cast<Json::UInt64>(
      lightweight_waiters_.load(std::memory_order_relaxed));
  jsv["running_tasks"] = static_cast<Json::UInt64>(permissions_granted_.size());
  jsv["max_tasks"] = static_cast<Json::UInt64>(max_tasks_);
  jsv["lightweight_task_overprovisioning"] =
      static_cast<Json::UInt64>(lightweight_task_overprovisioning_);

  for (auto&& e : permissions_granted_) {
    jsv["running_tasks"].append(static_cast<Json::UInt64>(e));
  }

  return jsv;
}

}  // namespace yadcc::daemon::local
