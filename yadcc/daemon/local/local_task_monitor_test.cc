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

#include <atomic>
#include <chrono>

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/fiber/fiber.h"
#include "flare/fiber/this_fiber.h"
#include "flare/fiber/timer.h"
#include "flare/testing/main.h"

DECLARE_int32(max_local_tasks);
DECLARE_double(lightweight_local_task_overprovisioning_ratio);

using namespace std::literals;

namespace yadcc::daemon::local {

TEST(LocalTaskMonitor, All) {
  FLAGS_max_local_tasks = 10;
  FLAGS_lightweight_local_task_overprovisioning_ratio = 2;

  // Kill its internal timer to prevent it from freeing task quota
  // automatically.
  flare::fiber::KillTimer(
      LocalTaskMonitor::Instance()->alive_process_check_timer_);
  flare::this_fiber::SleepFor(1s);

  // Upto 10 heavy tasks are allowed.
  for (int i = 0; i != 10; ++i) {
    auto result = LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
        i, false, 0ms);
    ASSERT_TRUE(result);
  }
  ASSERT_FALSE(LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
      11, false, 100ms));
  // [0, 10) running.

  // Lightweight tasks are still allowed.
  for (int i = 100; i != 120; ++i) {
    auto result = LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
        i, true, 0ms);
    ASSERT_TRUE(result);
  }
  // [0, 10), [100, 120) running.

  // Now both heavy and lightweight tasks are not allowed.
  ASSERT_FALSE(LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
      11, false, 100ms));
  ASSERT_FALSE(LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
      11, true, 100ms));
  // [0, 10), [100, 120) running.

  // New lightweight can start once a task finishes, whether it's a lightweight
  // task ...
  std::atomic<bool> granted{false};
  auto waiter = flare::Fiber([&] {
    granted = LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
        119, true, 1s);
  });
  LocalTaskMonitor::Instance()->DropTaskPermission(119);
  waiter.join();
  ASSERT_TRUE(granted);
  // ... or a heavy task.
  granted = false;
  waiter = flare::Fiber([&] {
    granted = LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
        120, true, 1s);
  });
  LocalTaskMonitor::Instance()->DropTaskPermission(0);
  waiter.join();
  ASSERT_TRUE(granted);
  // [1, 10), [100, 121) running.

  // But heavy task are not allowed to start in this case.
  granted = false;
  waiter = flare::Fiber([&] {
    granted = LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
        120, false, 1s);
  });
  LocalTaskMonitor::Instance()->DropTaskPermission(120);
  waiter.join();
  ASSERT_FALSE(granted);
  // [1, 10), [100, 120) running.

  // Until total task number drops below 10.
  for (int i = 100; i != 120; ++i) {
    LocalTaskMonitor::Instance()->DropTaskPermission(i);
  }
  ASSERT_TRUE(LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
      0, false, 0s));
  // [0, 10) running.

  // Reset a fake timer for its destructor to run correctly.
  LocalTaskMonitor::Instance()->alive_process_check_timer_ =
      flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), []() {});
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
