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

#include "yadcc/daemon/local/task_grant_keeper.h"

#include <chrono>

#include "gtest/gtest.h"

#include "flare/init/override_flag.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_mock.h"

#include "yadcc/api/scheduler.pb.h"

FLARE_OVERRIDE_FLAG(scheduler_uri, "mock://whatever-it-wants-to-be");

using namespace std::literals;

namespace yadcc::daemon::local {

scheduler::WaitForStartingTaskResponse MakeWaitForTaskResponse(int grant_id) {
  scheduler::WaitForStartingTaskResponse result;
  auto ptr = result.add_grants();
  ptr->set_task_grant_id(grant_id);
  return result;
}

TEST(DistributedTaskDispatcher, All) {
  std::atomic<std::size_t> freed_tasks{};
  FLARE_EXPECT_RPC(scheduler::SchedulerService::WaitForStartingTask,
                   ::testing::_)
      .WillRepeatedly(flare::testing::Return(MakeWaitForTaskResponse(1)));
  FLARE_EXPECT_RPC(scheduler::SchedulerService::FreeTask, ::testing::_)
      .WillRepeatedly(
          flare::testing::HandleRpc([&](auto&&...) { ++freed_tasks; }));

  TaskGrantKeeper keeper;

  auto result = keeper.Get(EnvironmentDesc(), 1s);
  ASSERT_TRUE(result);
  EXPECT_EQ(1, result->grant_id);

  keeper.Stop();
  keeper.Join();
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
