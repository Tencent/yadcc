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

#include "yadcc/client/task_quota.h"

#include "thirdparty/googletest/gmock/gmock.h"
#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/testing/hooking_mock.h"

#include "yadcc/client/daemon_call.h"

using namespace std::literals;

namespace yadcc::client {

TEST(TaskQuota, Timeout) {
  FLARE_EXPECT_HOOKED_CALL(DaemonCall, "/local/acquire_quota", ::testing::_,
                           ::testing::_, ::testing::_)
      .WillRepeatedly(::testing::Return(DaemonResponse{503, ""}));
  EXPECT_EQ(nullptr, TryAcquireTaskQuota(true, 10s));
}

TEST(TaskQuota, OK) {
  std::shared_ptr<void> handle;
  FLARE_EXPECT_HOOKED_CALL(DaemonCall, "/local/acquire_quota", ::testing::_,
                           ::testing::_, ::testing::_)
      .WillRepeatedly(::testing::Return(DaemonResponse{200, ""}));
  handle = AcquireTaskQuota(true);
  FLARE_EXPECT_HOOKED_CALL(DaemonCall, "/local/release_quota", ::testing::_,
                           ::testing::_, ::testing::_)
      .WillRepeatedly(::testing::Return(DaemonResponse{200, ""}));
  handle.reset();
}

}  // namespace yadcc::client
