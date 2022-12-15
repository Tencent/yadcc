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

#include "yadcc/scheduler/running_task_bookkeeper.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace yadcc::scheduler {

TEST(RunningTaskBookkeeper, ALL) {
  RunningTaskBookkeeper running_task_bookkeeper;
  std::vector<RunningTask> tasks;
  for (int i = 0; i < 3; ++i) {
    tasks.emplace_back();
    tasks.back().set_servant_task_id(i + 100);
  }
  running_task_bookkeeper.SetServantRunningTasks("my location", tasks);
  std::vector<std::uint64_t> result;
  for (auto r : running_task_bookkeeper.GetRunningTasks()) {
    result.push_back(r.servant_task_id());
  }

  EXPECT_THAT(result, ::testing::ElementsAre(100, 101, 102));

  running_task_bookkeeper.DropServant("my location");

  EXPECT_TRUE(running_task_bookkeeper.GetRunningTasks().empty());
}

}  // namespace yadcc::scheduler
