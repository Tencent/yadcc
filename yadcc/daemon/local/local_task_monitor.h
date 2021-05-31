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

#ifndef YADCC_DAEMON_LOCAL_LOCAL_TASK_MONITOR_H_
#define YADCC_DAEMON_LOCAL_LOCAL_TASK_MONITOR_H_

#include <chrono>
#include <cinttypes>
#include <unordered_set>

#include "thirdparty/googletest/gtest/gtest_prod.h"
#include "thirdparty/jsoncpp/json.h"

#include "flare/base/exposed_var.h"
#include "flare/fiber/condition_variable.h"
#include "flare/fiber/mutex.h"

namespace yadcc::daemon::local {

// This class monitors tasks started locally, and caps (when necessary) new task
// start-up. So as not to overwhelm local machine.
class LocalTaskMonitor {
 public:
  static LocalTaskMonitor* Instance();

  LocalTaskMonitor();
  ~LocalTaskMonitor();

  // Wait until the number of runnings local tasks drops below a pre-configured
  // threshold.
  //
  // Internally this monitor monitors existence of process with ID
  // `starting_task_pid`. Even if `DropTaskPermission` is not paired with a call
  // to this method (e.g., because of a crash of the task), the allocation is
  // released once that process cease to exist.
  //
  // Returns `false` if the call timeout before a new start can be started.
  bool WaitForRunningNewTaskPermission(pid_t starting_task_pid,
                                       bool lightweight,
                                       std::chrono::nanoseconds timeout);

  // Called to actively drop a previous `WaitForRunningNewTaskPermission` call.
  //
  // Although not strictly necessary, calling this method proactively (instead
  // of letting the monitor to detect its termination) allows new task to run in
  // a more timely fashion.
  void DropTaskPermission(pid_t pid);

 private:
  FRIEND_TEST(LocalTaskMonitor, All);

  void OnAliveProcessCheck();

  Json::Value DumpInternals();

 private:
  std::size_t max_tasks_;
  std::size_t lightweight_task_overprovisioning_;

  // For debugging purpose.
  std::atomic<std::size_t> heavyweight_waiters_, lightweight_waiters_{};

  std::uint64_t alive_process_check_timer_;

  // All permission we've granted.
  flare::fiber::Mutex permission_lock_;
  flare::fiber::ConditionVariable permission_cv_;
  std::unordered_set<pid_t> permissions_granted_;

  flare::ExposedVarDynamic<Json::Value> internal_exposer_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_LOCAL_TASK_MONITOR_H_
