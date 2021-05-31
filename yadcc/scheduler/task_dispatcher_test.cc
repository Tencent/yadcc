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

#include "thirdparty/googletest/gmock/gmock.h"
#include "thirdparty/googletest/gtest/gtest.h"

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
  servant.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;

  TaskDispatcher::Instance()->KeepServantAlive(servant, 10s);

  // Request compiler is not available on any servant.
  {
    TaskPersonality task;

    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("not found");
    EXPECT_FALSE(TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s,
                                                                    1s, false));
  }

  // Allocate 10 tasks.
  std::vector<TaskAllocation> tasks;
  for (int i = 0; i != 10; ++i) {
    TaskPersonality task;

    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("digest");

    auto allocation =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 5s, 1s, false);
    ASSERT_TRUE(allocation);
    EXPECT_EQ("127.0.0.1:1234", allocation->servant_location);
    tasks.push_back(*allocation);
  }

  // No servant available.
  {
    auto start = flare::ReadCoarseSteadyClock();
    TaskPersonality task;

    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("not found");
    EXPECT_FALSE(TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s,
                                                                    1s, false));
    EXPECT_NEAR((flare::ReadCoarseSteadyClock() - start) / 1ms, 1s / 1ms,
                100ms / 1ms);
  }

  // Unrecognized task ID.
  EXPECT_FALSE(TaskDispatcher::Instance()->KeepTaskAlive(12345678, 1s));

  // Keep existing tasks alive.
  for (auto&& e : tasks) {
    EXPECT_TRUE(TaskDispatcher::Instance()->KeepTaskAlive(e.task_id, 1s));
  }

  // 1000002,1000003 is not recognized by the dispatcher and should be killed.
  EXPECT_THAT(TaskDispatcher::Instance()->ExamineRunningTasks(
                  "127.0.0.1:1234", {tasks[0].task_id, 1000002, 1000003}),
              ::testing::ElementsAre(1000002, 1000003));

  std::this_thread::sleep_for(2s);
  // All tasks are expired by now.

  for (auto&& e : tasks) {
    EXPECT_FALSE(TaskDispatcher::Instance()->KeepTaskAlive(e.task_id, 1s));
  }

  {
    std::vector<std::uint64_t> task_ids;

    for (auto&& e : tasks) {
      task_ids.push_back(e.task_id);
    }

    EXPECT_EQ(task_ids, TaskDispatcher::Instance()->ExamineRunningTasks(
                            "127.0.0.1:1234", task_ids));
  }

  TaskDispatcher::Instance()->KeepServantAlive(servant, 1s);

  std::this_thread::sleep_for(2s);
  // The servant itself has expired by now.

  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.1";
    task.env_desc.set_compiler_digest("digest");

    // Now no task can be allocated.
    EXPECT_FALSE(TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s,
                                                                    1s, false));
  }
}

TEST(TaskDispatcher, PreferDedicated) {
  TaskPersonality task;

  task.requestor_ip = "127.0.0.1";
  task.env_desc.set_compiler_digest("digest");

  ServantPersonality servant;

  servant.observed_location = "127.0.0.1:1234";
  servant.reported_location = "127.0.0.1:1234";
  servant.environments.emplace_back().set_compiler_digest("digest");
  servant.max_tasks = 10;
  servant.current_load = 0;
  servant.num_processors = 10;
  servant.priority = SERVANT_PRIORITY_USER;
  servant.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;

  TaskDispatcher::Instance()->KeepServantAlive(servant, 1s);

  auto allocation =
      TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
  ASSERT_TRUE(allocation);
  EXPECT_EQ("127.0.0.1:1234", allocation->servant_location);
  TaskDispatcher::Instance()->FreeTask(allocation->task_id);

  servant.observed_location = "192.168.0.1:1234";
  servant.reported_location = "192.168.0.1:1234";
  servant.current_load = 2;  // Higher load.
  servant.priority = SERVANT_PRIORITY_DEDICATED;
  TaskDispatcher::Instance()->KeepServantAlive(servant, 1s);

  allocation =
      TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
  ASSERT_TRUE(allocation);
  // Prefer dedicated servant then even if it's load is higher.
  EXPECT_EQ("192.168.0.1:1234", allocation->servant_location);
  TaskDispatcher::Instance()->FreeTask(allocation->task_id);

  std::this_thread::sleep_for(1500ms);  // For servants to expire.
}

TEST(TaskDispatcher, LoadBalanceCase) {
  ServantPersonality servant_over_load;
  servant_over_load.observed_location = "192.168.0.0:0000";
  servant_over_load.reported_location = "192.168.0.0:0000";
  servant_over_load.environments.emplace_back().set_compiler_digest(
      "Load Balance");
  servant_over_load.max_tasks = 7;
  servant_over_load.num_processors = 16;
  servant_over_load.current_load = 16;
  servant_over_load.priority = SERVANT_PRIORITY_USER;  // Doesn't matter.
  servant_over_load.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;

  TaskDispatcher::Instance()->KeepServantAlive(servant_over_load, 10s);

  // Expected result: servant_over_load over load, never pick it
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    EXPECT_FALSE(TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s,
                                                                    1s, false));
  }

  ServantPersonality servant1;
  servant1.observed_location = "192.168.0.1:1111";
  servant1.reported_location = "192.168.0.1:1111";
  servant1.environments.emplace_back().set_compiler_digest("Load Balance");
  servant1.max_tasks = 7;
  servant1.num_processors = 16;
  servant1.current_load = 1;
  servant1.priority = SERVANT_PRIORITY_USER;  // Doesn't matter.
  servant1.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;

  ServantPersonality servant2;
  servant2.observed_location = "192.168.0.2:2222";
  servant2.reported_location = "192.168.0.2:2222";
  servant2.environments.emplace_back().set_compiler_digest("Load Balance");
  servant2.max_tasks = 8;
  servant2.num_processors = 16;
  servant2.current_load = 5;
  servant2.priority = SERVANT_PRIORITY_USER;  // Doesn't matter.
  servant2.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;

  ServantPersonality servant3;
  servant3.observed_location = "192.168.0.3:3333";
  servant3.reported_location = "192.168.0.3:3333";
  servant3.environments.emplace_back().set_compiler_digest("Load Balance");
  servant3.max_tasks = 6;
  servant3.num_processors = 16;
  servant3.current_load = 12;
  servant3.priority = SERVANT_PRIORITY_USER;  // Doesn't matter.
  servant3.memory_available_in_bytes = 50ULL * 1024 * 1024 * 1024;

  TaskDispatcher::Instance()->KeepServantAlive(servant1, 10s);
  TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  TaskDispatcher::Instance()->KeepServantAlive(servant3, 10s);

  // Expected result: servant1: 0 / 7, servant2: 0 / 8, servant3: 0 / 4, pick
  // servant1
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant1.observed_location, pick_up->servant_location);

    servant1.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant1, 10s);
  }

  // Expected result: servant1: 1 / 7, servant2: 0 / 8, servant3: 0 / 4, pick
  // servant2
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant2.observed_location, pick_up->servant_location);

    servant2.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  }
  // Expected result: servant1: 1 / 7, servant2: 1 / 8, servant3: 0 / 4, pick
  // servant3
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant3.observed_location, pick_up->servant_location);

    servant3.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant3, 10s);
  }

  // Expected result: servant1: 1 / 7, servant2: 1 / 8, servant3: 1 / 4, pick
  // servant2
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant2.observed_location, pick_up->servant_location);

    servant2.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  }

  // Expected result: servant1: 1 / 7, servant2: 2 / 8, servant3: 1 / 4, pick
  // servant1
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant1.observed_location, pick_up->servant_location);

    servant1.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant1, 10s);
  }

  // Expected result: servant1: 2 / 7, servant2: 2 / 8, servant3: 1 / 4, pick
  // servant2
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant2.observed_location, pick_up->servant_location);

    servant2.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant2, 10s);
  }

  // Expected result: servant1: 2 / 7, servant2: 3 / 8, servant3: 1 / 4, pick
  // servant3
  {
    TaskPersonality task;
    task.requestor_ip = "127.0.0.3";
    task.env_desc.set_compiler_digest("Load Balance");
    auto pick_up =
        TaskDispatcher::Instance()->WaitForStartingNewTask(task, 1s, 1s, false);
    EXPECT_EQ(servant3.observed_location, pick_up->servant_location);

    servant3.current_load += 1;  // Our task is running now.
    TaskDispatcher::Instance()->KeepServantAlive(servant3, 10s);
  }
}

}  // namespace yadcc::scheduler

FLARE_TEST_MAIN
