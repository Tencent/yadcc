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

#include "yadcc/daemon/cloud/execution_engine.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>

#include "gflags/gflags.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/init/override_flag.h"
#include "flare/testing/main.h"

#include "yadcc/daemon/cloud/temporary_file.h"

using namespace std::literals;

FLARE_OVERRIDE_FLAG(max_remote_tasks, 1);

namespace yadcc::daemon::cloud {

class TestingTask : public ExecutionTask {
 public:
  std::string GetCommandLine() const override { return cmd; }

  flare::NoncontiguousBuffer GetStandardInputOnce() override { return input; }

  void OnCompletion(int ec, flare::NoncontiguousBuffer standard_output,
                    flare::NoncontiguousBuffer standard_error) override {
    exit_code = ec;
    output = flare::FlattenSlow(standard_output);
    error = flare::FlattenSlow(standard_error);
    completed = true;
    ++times_called;
  }

  Json::Value DumpInternals() const override { return {}; }

 public:
  std::atomic<bool> completed{};
  std::string cmd;
  flare::NoncontiguousBuffer input;
  int exit_code;
  std::string output, error;

  // You need to reinitialize it yourself.
  inline static std::atomic<int> times_called{};
};

ExecutionEngine::Input CreateDummyInput() {
  return ExecutionEngine::Input{.standard_input = TemporaryFile("."),
                                .standard_output = TemporaryFile("."),
                                .standard_error = TemporaryFile(".")};
}

std::vector<std::uint64_t> GetRunningTask() {
  std::vector<std::uint64_t> result;
  auto running_tasks = ExecutionEngine::Instance()->EnumerateTasks();
  std::transform(
      running_tasks.begin(), running_tasks.end(),
      std::back_insert_iterator(result),
      [](const ExecutionEngine::Task& e) { return e.task_grant_id; });
  return result;
}

void DummyCallback(ExecutionEngine::Output*) {}

flare::RefPtr<TestingTask> MakeTestingTask(const std::string& cmd,
                                           const std::string& input) {
  auto result = flare::MakeRefCounted<TestingTask>();
  result->cmd = cmd;
  result->input = flare::CreateBufferSlow(input);
  return result;
}

TEST(ExecutionEngine, Task) {
  EXPECT_EQ(1, *ExecutionEngine::Instance()->GetMaximumTasks());
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ = 0;

  // IO.
  auto cat_task = ExecutionEngine::Instance()->TryQueueTask(
      1, MakeTestingTask("/bin/cat", "hello"));
  ASSERT_TRUE(cat_task);
  auto wait_result = ExecutionEngine::Instance()->WaitForTask(*cat_task, 10s);
  EXPECT_TRUE(wait_result);
  auto result = static_cast<TestingTask*>(wait_result->Get());
  EXPECT_TRUE(result->completed);

  EXPECT_EQ(0, result->exit_code);
  EXPECT_EQ("hello", result->output);
  EXPECT_EQ("", result->error);

  // Exit code.
  auto false_task = ExecutionEngine::Instance()->TryQueueTask(
      2, MakeTestingTask("/bin/false", ""));
  ASSERT_TRUE(false_task);
  wait_result = ExecutionEngine::Instance()->WaitForTask(*false_task, 10s);
  EXPECT_TRUE(wait_result);
  result = static_cast<TestingTask*>(wait_result->Get());
  EXPECT_EQ(1, result->exit_code);

  // Long running task.
  auto sleeping_task = ExecutionEngine::Instance()->TryQueueTask(
      123, MakeTestingTask("/bin/sleep 1000", ""));
  ASSERT_TRUE(sleeping_task);
  wait_result = ExecutionEngine::Instance()->WaitForTask(*sleeping_task, 1s);
  EXPECT_FALSE(wait_result);

  // Max running task limit reached, new task should fail.
  EXPECT_FALSE(ExecutionEngine::Instance()->TryQueueTask(
      3, MakeTestingTask("/bin/cat", "")));

  // Kill task asynchronously.

  EXPECT_THAT(GetRunningTask(), ::testing::UnorderedElementsAre(1, 2, 123));
  ExecutionEngine::Instance()->KillExpiredTasks({123});
  std::this_thread::sleep_for(1s);  // Wait for `/bin/sleep` termination.

  // Internal state consistency.
  EXPECT_THAT(GetRunningTask(), ::testing::UnorderedElementsAre(123, 1, 2));
  ExecutionEngine::Instance()->FreeTask(*cat_task);
  ExecutionEngine::Instance()->FreeTask(*false_task);
  EXPECT_THAT(GetRunningTask(), ::testing::UnorderedElementsAre(123));
  ExecutionEngine::Instance()->FreeTask(*sleeping_task);
  EXPECT_TRUE(ExecutionEngine::Instance()->EnumerateTasks().empty());
}

TEST(ExecutionEngine, RejectOnMemoryFull) {
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ =
      std::numeric_limits<std::size_t>::max();

  auto cat_task = ExecutionEngine::Instance()->TryQueueTask(
      1, MakeTestingTask("/bin/cat", ""));
  ASSERT_FALSE(cat_task);
}

TEST(ExecutionEngine, Stability) {
  // Tests if the engine can recover from low memory condition.
  ExecutionEngine::Instance()->task_concurrency_limit_ = 10;
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ =
      std::numeric_limits<std::size_t>::max();
  for (int i = 0; i != 100; ++i) {
    auto cat_task = ExecutionEngine::Instance()->TryQueueTask(
        1, MakeTestingTask("/bin/cat", ""));
    ASSERT_FALSE(cat_task);
  }

  TestingTask::times_called = 0;
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ = 0;
  for (int i = 0; i != 1000; ++i) {
    TemporaryFile input(".");
    input.Write(flare::CreateBufferSlow("hello"));
    auto cat_task = ExecutionEngine::Instance()->TryQueueTask(
        1, MakeTestingTask("/bin/cat", "hello"));
    ASSERT_TRUE(cat_task);
    auto result = ExecutionEngine::Instance()->WaitForTask(*cat_task, 10s);
    EXPECT_TRUE(result);
    ExecutionEngine::Instance()->FreeTask(*cat_task);
  }
  EXPECT_EQ(1000, TestingTask::times_called);

  ExecutionEngine::Instance()->Stop();
  ExecutionEngine::Instance()->Join();
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
