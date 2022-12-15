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

#include <chrono>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "flare/testing/main.h"

using namespace std::literals;

namespace yadcc::scheduler {

TEST(TaskDispatcher, All) {
  ServantPersonality servant;

  servant.observed_location = "127.0.0.1:1234";
  servant.reported_location = "127.0.0.1:1234";
  servant.environments.emplace_back().set_compiler_digest("digest");
  servant.max_tasks = 10;
  servant.current_load = 0;
  servant.num_processors = 10;
  servant.priority = SERVANT_PRIORITY_USER;  // Doesn't matter.
  servant.version = 8;
  servant.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;
  TaskDispatcher::Instance()->KeepServantAlive(servant, 10s);

  // No environment available.
  {
    auto start = flare::ReadCoarseSteadyClock();
    TaskPersonality task;

    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("not found");
    task.min_version = 8;

    auto result = TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, 1s, flare::ReadCoarseSteadyClock() + 1s, false);
    ASSERT_FALSE(result);
    EXPECT_EQ(WaitStatus::EnvironmentNotFound, result.error());
  }

  // Allocate 10 tasks.
  std::vector<TaskAllocation> tasks;
  for (int i = 0; i != 10; ++i) {
    TaskPersonality task;

    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("digest");
    task.min_version = 8;

    auto allocation = TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, 5s, flare::ReadCoarseSteadyClock() + 1s, false);
    ASSERT_TRUE(allocation);
    EXPECT_EQ("127.0.0.1:1234", allocation->servant_location);
    tasks.push_back(*allocation);
  }

  // No servant available.
  {
    auto start = flare::ReadCoarseSteadyClock();
    TaskPersonality task;
    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("digest");
    task.min_version = 8;

    auto result = TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, 1s, flare::ReadCoarseSteadyClock() + 1s, false);
    ASSERT_FALSE(result);
    EXPECT_EQ(WaitStatus::Timeout, result.error());

    EXPECT_NEAR((flare::ReadCoarseSteadyClock() - start) / 1ms, 1s / 1ms,
                100ms / 1ms);
  }

  // Unrecognized task ID.
  EXPECT_FALSE(TaskDispatcher::Instance()->KeepTaskAlive(12345678, 1s));

  // Keep existing tasks alive.
  for (auto&& e : tasks) {
    EXPECT_TRUE(TaskDispatcher::Instance()->KeepTaskAlive(e.task_id, 1s));
  }

  std::vector<RunningTask> running_tasks;
  running_tasks.emplace_back().set_task_grant_id(tasks[0].task_id);
  running_tasks.emplace_back().set_task_grant_id(1000002);
  running_tasks.emplace_back().set_task_grant_id(1000003);

  // 1000002,1000003 is not recognized by the dispatcher and should be killed.
  EXPECT_THAT(TaskDispatcher::Instance()->NotifyServantRunningTasks(
                  "127.0.0.1:1234", std::move(running_tasks)),
              ::testing::ElementsAre(1000002, 1000003));

  std::this_thread::sleep_for(2s);
  // All tasks are expired by now.

  for (auto&& e : tasks) {
    EXPECT_FALSE(TaskDispatcher::Instance()->KeepTaskAlive(e.task_id, 1s));
  }

  {
    std::vector<std::uint64_t> task_ids;
    std::vector<RunningTask> running_tasks;
    for (auto&& e : tasks) {
      task_ids.push_back(e.task_id);
      running_tasks.emplace_back();
      running_tasks.back().set_task_grant_id(e.task_id);
    }

    EXPECT_EQ(task_ids, TaskDispatcher::Instance()->NotifyServantRunningTasks(
                            "127.0.0.1:1234", std::move(running_tasks)));
  }

  TaskDispatcher::Instance()->KeepServantAlive(servant, 1s);

  std::this_thread::sleep_for(2s);
  // The servant itself has expired by now.

  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("digest");
    task.min_version = 8;

    // Now no task can be allocated.
    EXPECT_FALSE(TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, 1s, flare::ReadCoarseSteadyClock() + 1s, false));
  }
}

TEST(TaskDispatcher, PreferDedicated) {
  TaskPersonality task;

  task.requestor_ip = "127.0.0.1";
  task.env_desc.set_compiler_digest("digest");
  task.min_version = 8;

  ServantPersonality servant;

  servant.observed_location = "127.0.0.1:1234";
  servant.reported_location = "127.0.0.1:1234";
  servant.environments.emplace_back().set_compiler_digest("digest");
  servant.max_tasks = 10;
  servant.current_load = 0;
  servant.num_processors = 10;
  servant.priority = SERVANT_PRIORITY_USER;
  servant.version = 8;
  servant.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;
  TaskDispatcher::Instance()->KeepServantAlive(servant, 1s);

  auto allocation = TaskDispatcher::Instance()->WaitForStartingNewTask(
      task, 1s, flare::ReadCoarseSteadyClock() + 1s, false);
  ASSERT_TRUE(allocation);
  EXPECT_EQ("127.0.0.1:1234", allocation->servant_location);
  TaskDispatcher::Instance()->FreeTask(allocation->task_id);

  servant.observed_location = "192.168.0.1:1234";
  servant.reported_location = "192.168.0.1:1234";
  servant.current_load = 2;  // Higher load.
  servant.priority = SERVANT_PRIORITY_DEDICATED;
  TaskDispatcher::Instance()->KeepServantAlive(servant, 1s);

  allocation = TaskDispatcher::Instance()->WaitForStartingNewTask(
      task, 1s, flare::ReadCoarseSteadyClock() + 1s, false);
  ASSERT_TRUE(allocation);
  // Prefer dedicated servant then even if it's load is higher.
  EXPECT_EQ("192.168.0.1:1234", allocation->servant_location);
  TaskDispatcher::Instance()->FreeTask(allocation->task_id);

  std::this_thread::sleep_for(1500ms);  // For servants to expire.
}

ServantPersonality AddServant(const std::string location, std::size_t max_tasks,
                              std::size_t num_processors, std::size_t load,
                              std::size_t memory_available_in_bytes) {
  ServantPersonality servant;
  servant.observed_location = location;
  servant.reported_location = location;
  servant.environments.emplace_back().set_compiler_digest("Load Balance");
  servant.max_tasks = max_tasks;
  servant.num_processors = num_processors;
  servant.current_load = load;
  servant.priority = SERVANT_PRIORITY_USER;  // Doesn't matter.
  servant.version = 8;
  servant.memory_available_in_bytes = memory_available_in_bytes;
  TaskDispatcher::Instance()->KeepServantAlive(servant, 10s);
  return servant;
}

void ExpectServant(const std::string& requestor_ip,
                   const std::string& expected_location) {
  TaskPersonality task;
  task.requestor_ip = requestor_ip;
  task.env_desc.set_compiler_digest("Load Balance");
  task.min_version = 8;
  auto pick_up = TaskDispatcher::Instance()->WaitForStartingNewTask(
      task, 1s, flare::ReadCoarseSteadyClock() + 1s, false);
  EXPECT_EQ(expected_location, pick_up->servant_location);
}

TEST(TaskDispatcher, LoadBalanceCase) {
  ServantPersonality servant_over_load =
      AddServant("192.168.0.0:0000", 7, 16, 16, 50ULL * 1024 * 1024 * 1024);

  // Expected result: servant_over_load over load, never pick it
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    task.min_version = 8;
    EXPECT_FALSE(TaskDispatcher::Instance()->WaitForStartingNewTask(
        task, 1s, flare::ReadCoarseSteadyClock() + 1s, false));
  }

  auto servant1 =
      AddServant("192.168.0.1:1111", 7, 16, 1, 50ULL * 1024 * 1024 * 1024);
  auto servant2 =
      AddServant("192.168.0.2:2222", 8, 16, 5, 50ULL * 1024 * 1024 * 1024);
  auto servant3 =
      AddServant("192.168.0.3:3333", 6, 16, 12, 50ULL * 1024 * 1024 * 1024);

  // Expected result: servant1: 0 / 7, servant2: 0 / 8, servant3: 0 / 4, pick
  // servant1
  {
    ExpectServant("127.0.0.3", servant1.observed_location);

    servant1.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant1, 10s);
  }

  // Expected result: servant1: 1 / 7, servant2: 0 / 8, servant3: 0 / 4, pick
  // servant2
  {
    ExpectServant("127.0.0.3", servant2.observed_location);

    servant2.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  }
  // Expected result: servant1: 1 / 7, servant2: 1 / 8, servant3: 0 / 4, pick
  // servant3
  {
    ExpectServant("127.0.0.3", servant3.observed_location);

    servant3.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant3, 10s);
  }

  // Expected result: servant1: 1 / 7, servant2: 1 / 8, servant3: 1 / 4, pick
  // servant2
  {
    ExpectServant("127.0.0.3", servant2.observed_location);

    servant2.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  }

  // Expected result: servant1: 1 / 7, servant2: 2 / 8, servant3: 1 / 4, pick
  // servant1
  {
    ExpectServant("127.0.0.3", servant1.observed_location);

    servant1.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant1, 10s);
  }

  // Expected result: servant1: 2 / 7, servant2: 2 / 8, servant3: 1 / 4, pick
  // servant2
  {
    ExpectServant("127.0.0.3", servant2.observed_location);

    servant2.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  }

  // Expected result: servant1: 2 / 7, servant2: 3 / 8, servant3: 1 / 4, pick
  // servant3
  {
    ExpectServant("127.0.0.3", servant3.observed_location);

    servant3.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant3, 10s);
  }
}

}  // namespace yadcc::scheduler

FLARE_TEST_MAIN
