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

#include "yadcc/daemon/cloud/compiler_registry.h"

#include "thirdparty/googletest/gtest/gtest.h"

namespace yadcc::daemon::cloud {

// BLAKE3 of `testdata/gcc`.
constexpr auto kTestCompilerDigest =
    "a609c75b33a74f24e3bdf474d206cb486e0ea2b353e87886e849b505678d4ea7";

TEST(CompilerRegistry, All) {
  CompilerRegistry::Instance()->RegisterEnvironment("./test-bin/gcc");

  auto envs = CompilerRegistry::Instance()->EnumerateEnvironments();
  EXPECT_GE(envs.size(), 1);

  EXPECT_NE(envs.end(), std::find_if(envs.begin(), envs.end(), [&](auto&& e) {
              return e.compiler_digest() == kTestCompilerDigest;
            }));

  EnvironmentDesc desc;

  desc.set_compiler_digest(kTestCompilerDigest);
  EXPECT_EQ("./test-bin/gcc",
            CompilerRegistry::Instance()->TryGetCompilerPath(desc));

  desc.set_compiler_digest("something won't be there");
  EXPECT_EQ(std::nullopt,
            CompilerRegistry::Instance()->TryGetCompilerPath(desc));
}

}  // namespace yadcc::daemon::cloud
