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

#include "yadcc/daemon/cloud/temporary_file.h"

#include <unistd.h>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/string.h"

namespace yadcc::daemon::cloud {

TEST(TemporaryFile, All) {
  TemporaryFile file("/tmp");

  EXPECT_TRUE(flare::StartsWith(file.GetPath(), "/tmp/"));
  EXPECT_NE(0, file.fd());
  file.Write(flare::CreateBufferSlow("hello"));
  EXPECT_EQ("hello", flare::FlattenSlow(file.ReadAll()));
}

}  // namespace yadcc::daemon::cloud
