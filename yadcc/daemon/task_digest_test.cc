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

#include "yadcc/daemon/task_digest.h"

#include "gtest/gtest.h"

#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding.h"

namespace yadcc::daemon {

TEST(TaskDigest, ALL) {
  EnvironmentDesc env_desc;
  env_desc.set_compiler_digest("my compiler digest");
  EXPECT_EQ(flare::EncodeHex(
                flare::Blake3({"cxx2", env_desc.compiler_digest(),
                               "my invocation arguments", "my source digest"})),
            GetCxxTaskDigest(env_desc, "my invocation arguments",
                             "my source digest"));
}

}  // namespace yadcc::daemon
