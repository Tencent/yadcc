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

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/googletest/gmock/gmock.h"
#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/init/override_flag.h"
#include "flare/testing/main.h"

#include "yadcc/daemon/cloud/temporary_file.h"

using namespace std::literals;

FLARE_OVERRIDE_FLAG(max_remote_tasks, 1);

namespace yadcc::daemon::cloud {

ExecutionEngine::Input CreateDummyInput() {
  return ExecutionEngine::Input{.standard_input = TemporaryFile("."),
                                .standard_output = TemporaryFile("."),
                                .standard_error = TemporaryFile(".")};
}

TEST(ExecutionEngine, All) {
  EXPECT_EQ(1, *ExecutionEngine::Instance()->GetMaximumTasks());

  TemporaryFile input(".");
  input.Write(flare::CreateBufferSlow("hello"));
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ = 0;

  // IO.
  auto cat_task = ExecutionEngine::Instance()->TryQueueCommandForExecution(
      1, "/bin/cat",
      ExecutionEngine::Input{
          .standard_input = std::move(input),
          .standard_output = TemporaryFile("."),
          .standard_error = TemporaryFile("."),
          .context = std::make_shared<std::string>("my context")});
  ASSERT_TRUE(cat_task);
  auto result = ExecutionEngine::Instance()->WaitForCompletion(*cat_task, 10s);
  EXPECT_TRUE(result);

  EXPECT_EQ(0, result->exit_code);
  EXPECT_EQ("hello", flare::FlattenSlow(result->standard_output));
  EXPECT_EQ("", flare::FlattenSlow(result->standard_error));
  EXPECT_EQ("my context", *static_cast<std::string*>(result->context.get()));

  // Exit code.
  auto false_task = ExecutionEngine::Instance()->TryQueueCommandForExecution(
      2, "/bin/false", CreateDummyInput());
  ASSERT_TRUE(false_task);
  result = ExecutionEngine::Instance()->WaitForCompletion(*false_task, 10s);
  EXPECT_TRUE(result);
  EXPECT_EQ(1, result->exit_code);

  // Long running task.
  auto sleeping_task = ExecutionEngine::Instance()->TryQueueCommandForExecution(
      123, "/bin/sleep 1000", CreateDummyInput());
  ASSERT_TRUE(sleeping_task);
  result = ExecutionEngine::Instance()->WaitForCompletion(*sleeping_task, 1s);
  EXPECT_FALSE(result);

  // Max running task limit reached, new task should fail.
  EXPECT_FALSE(ExecutionEngine::Instance()->TryQueueCommandForExecution(
      3, "/bin/cat", CreateDummyInput()));

  // Kill task asynchronously.
  EXPECT_THAT(ExecutionEngine::Instance()->EnumerateGrantOfRunningTask(),
              ::testing::UnorderedElementsAre(1, 2, 123));
  ExecutionEngine::Instance()->KillExpiredTasks({123});
  std::this_thread::sleep_for(1s);  // Wait for `/bin/sleep` termination.

  // Internal state consistency.
  EXPECT_THAT(ExecutionEngine::Instance()->EnumerateGrantOfRunningTask(),
              ::testing::UnorderedElementsAre(123, 1, 2));
  ExecutionEngine::Instance()->FreeTask(*cat_task);
  ExecutionEngine::Instance()->FreeTask(*false_task);
  EXPECT_THAT(ExecutionEngine::Instance()->EnumerateGrantOfRunningTask(),
              ::testing::UnorderedElementsAre(123));
  ExecutionEngine::Instance()->FreeTask(*sleeping_task);
  EXPECT_TRUE(
      ExecutionEngine::Instance()->EnumerateGrantOfRunningTask().empty());
}

TEST(ExecutionEngine, RejectOnMemoryFull) {
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ =
      std::numeric_limits<std::size_t>::max();

  auto cat_task = ExecutionEngine::Instance()->TryQueueCommandForExecution(
      1, "/bin/cat", ExecutionEngine::Input{});
  ASSERT_FALSE(cat_task);
}

TEST(ExecutionEngine, Stability) {
  // Tests if the engine can recover from low memory condition.
  ExecutionEngine::Instance()->task_concurrency_limit_ = 10;
  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ =
      std::numeric_limits<std::size_t>::max();
  for (int i = 0; i != 100; ++i) {
    auto cat_task = ExecutionEngine::Instance()->TryQueueCommandForExecution(
        1, "/bin/cat", ExecutionEngine::Input{});
    ASSERT_FALSE(cat_task);
  }

  ExecutionEngine::Instance()->min_memory_for_starting_new_task_ = 0;
  for (int i = 0; i != 1000; ++i) {
    TemporaryFile input(".");
    input.Write(flare::CreateBufferSlow("hello"));
    auto cat_task = ExecutionEngine::Instance()->TryQueueCommandForExecution(
        1, "/bin/cat",
        ExecutionEngine::Input{
            .standard_input = std::move(input),
            .standard_output = TemporaryFile("."),
            .standard_error = TemporaryFile("."),
            .context = std::make_shared<std::string>("my context")});
    ASSERT_TRUE(cat_task);
    auto result =
        ExecutionEngine::Instance()->WaitForCompletion(*cat_task, 10s);
    EXPECT_TRUE(result);
    ExecutionEngine::Instance()->FreeTask(*cat_task);
  }
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
