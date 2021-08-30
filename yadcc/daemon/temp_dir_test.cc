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

#include "yadcc/daemon/temp_dir.h"

#include "gtest/gtest.h"

#include "flare/testing/hooking_mock.h"

#include "yadcc/daemon/sysinfo.h"

namespace yadcc::daemon {

std::string DetermineTemporaryDirectory();

TEST(DaemonService, UseDevShm) {
  FLARE_EXPECT_HOOKED_CALL(GetDiskAvailableSize, ::testing::_)
      .WillRepeatedly(::testing::Return(1048576000000));
  EXPECT_EQ("/dev/shm", DetermineTemporaryDirectory());
}

TEST(DaemonService, UseTmp) {
  FLARE_EXPECT_HOOKED_CALL(GetDiskAvailableSize, ::testing::_)
      .WillRepeatedly(::testing::Return(1048576));
  EXPECT_EQ("/tmp", DetermineTemporaryDirectory());
}

}  // namespace yadcc::daemon
