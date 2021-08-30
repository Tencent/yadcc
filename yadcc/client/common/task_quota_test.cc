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

#include "yadcc/client/common/task_quota.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "yadcc/client/common/daemon_call.h"

using namespace std::literals;

namespace yadcc::client {

TEST(TaskQuota, Timeout) {
  SetDaemonCallGatheredHandler([](auto&&...) { return DaemonResponse{503}; });
  EXPECT_EQ(nullptr, TryAcquireTaskQuota(true, 10s));
}

TEST(TaskQuota, OK) {
  std::shared_ptr<void> handle;
  SetDaemonCallGatheredHandler([](auto&&...) { return DaemonResponse{200}; });
  handle = AcquireTaskQuota(true);
  handle.reset();
}

}  // namespace yadcc::client
