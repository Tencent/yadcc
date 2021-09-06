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

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/string.h"

DECLARE_string(extra_compiler_dirs);

namespace yadcc::daemon::cloud {

// BLAKE3 of `native-gcc/gcc`.
constexpr auto kNativeGccDigest =
    "ed756eba1ad5a11b557851c99e9d900f21a0d4e32f590a950019f132569bc834";
// `symlink-gcc/gcc`
constexpr auto kSymlinkGccDigest =
    "31eabbe1a118bd886aa87dda7139b2beb1e5fdec06bdf2686be9ed9dfe0ed65f";
// `symlink-ccache/gcc`
constexpr auto kSymlinkCcacheDigest =
    "913cefa83a568d15f15d84d715e449a002364a9c6079fb46a56d1381cfb773f5";

TEST(CompilerRegistry, All) {
  FLAGS_extra_compiler_dirs =
      "test-bin/native-gcc/:"
      "test-bin/symlink-ccache/:"
      "test-bin/symlink-gcc/:"
      "test-bin/symlink-to-404/";

  // blade does not handle symlinks in `testdata` well, so we generate the
  // symlinks ourselves.
  (void)symlink("./ccache", "test-bin/symlink-ccache/gcc");
  (void)symlink("./gcc-9", "test-bin/symlink-gcc/gcc");
  (void)symlink("./404", "test-bin/symlink-to-404/gcc");

  auto envs = CompilerRegistry::Instance()->EnumerateEnvironments();
  EXPECT_GE(envs.size(), 2);

  EXPECT_NE(envs.end(), std::find_if(envs.begin(), envs.end(), [&](auto&& e) {
              return e.compiler_digest() == kNativeGccDigest;
            }));

  EnvironmentDesc desc;

  desc.set_compiler_digest(kNativeGccDigest);
  EXPECT_TRUE(
      flare::EndsWith(*CompilerRegistry::Instance()->TryGetCompilerPath(desc),
                      "test-bin/native-gcc/gcc"));
  desc.set_compiler_digest(kSymlinkGccDigest);
  EXPECT_TRUE(
      flare::EndsWith(*CompilerRegistry::Instance()->TryGetCompilerPath(desc),
                      "test-bin/symlink-gcc/gcc-9"));
  desc.set_compiler_digest(kSymlinkCcacheDigest);
  EXPECT_EQ(std::nullopt,
            CompilerRegistry::Instance()->TryGetCompilerPath(desc));

  desc.set_compiler_digest("something won't be there");
  EXPECT_EQ(std::nullopt,
            CompilerRegistry::Instance()->TryGetCompilerPath(desc));
}

}  // namespace yadcc::daemon::cloud
