// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "yadcc/daemon/local/running_task_keeper.h"

#include <thread>

#include "flare/init/override_flag.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_mock.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "yadcc/daemon/task_digest.h"

FLARE_OVERRIDE_FLAG(scheduler_uri, "mock://whatever-it-wants-to-be");

using namespace std::literals;

namespace yadcc::daemon::local {

void GetRunningTasksHandler(const scheduler::GetRunningTasksRequest& request,
                            scheduler::GetRunningTasksResponse* response,
                            flare::RpcServerController* controller) {
  static auto count = 0;
  if (++count <= 2) {
    for (int i = 0; i < 3; ++i) {
      auto running_task = response->add_running_tasks();
      running_task->set_servant_task_id(i);
      running_task->set_task_grant_id(i + 100);
      running_task->set_task_digest("task digest" + std::to_string(i));
    }
  }
}

TEST(RunningTaskKeeper, ALL) {
  FLARE_EXPECT_RPC(scheduler::SchedulerService::GetRunningTasks, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(GetRunningTasksHandler));

  RunningTaskKeeper running_task_keeper;
  std::this_thread::sleep_for(1s);
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(
        !!running_task_keeper.TryFindTask("task digest" + std::to_string(i)));
  }

  std::this_thread::sleep_for(2s);
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(
        !!running_task_keeper.TryFindTask("task digest" + std::to_string(i)));
  }

  running_task_keeper.Stop();
  std::this_thread::sleep_for(1s);  // Wait for timer to be fully killed.
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
