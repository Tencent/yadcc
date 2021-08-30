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

#include <unistd.h>
#include <cstdio>

#include "gflags/gflags.h"
#include "gtest/gtest.h"

#include "flare/base/chrono.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/this_fiber.h"
#include "flare/init/override_flag.h"
#include "flare/testing/main.h"

#include "yadcc/common/dir.h"

using namespace std::literals;

DECLARE_string(extra_compiler_dirs);
DECLARE_string(extra_compiler_bundle_dirs);
FLARE_OVERRIDE_FLAG(compiler_rescan_interval, 1);

namespace yadcc::daemon::cloud {

// BLAKE3 of `testdata/gcc`.
constexpr auto kTestCompilerDigest =
    "a609c75b33a74f24e3bdf474d206cb486e0ea2b353e87886e849b505678d4ea7";

int QueryDynamicCompilers() {
  auto compilers = CompilerRegistry::Instance()->EnumerateEnvironments();
  return compilers.size();
}

TEST(CompilerRegistry, All) {
  FLAGS_extra_compiler_bundle_dirs = "./test-bin:/path/not/exist";
  FLAGS_extra_compiler_dirs = "./test-bin:/path/not/exist";
  std::unordered_map<std::string, std::string> temp_paths;
  std::vector<EnvironmentDesc> temp_envs;

  CompilerRegistry::Instance()->OnCompilerRescanTimer();

  auto envs = CompilerRegistry::Instance()->EnumerateEnvironments();
  EXPECT_GE(envs.size(), 1);
  EXPECT_NE(envs.end(), std::find_if(envs.begin(), envs.end(), [&](auto&& e) {
              return e.compiler_digest() == kTestCompilerDigest;
            }));

  EnvironmentDesc desc;

  desc.set_compiler_digest(kTestCompilerDigest);
  EXPECT_EQ(GetCanonicalPath("./test-bin/gcc"),
            CompilerRegistry::Instance()->TryGetCompilerPath(desc));

  desc.set_compiler_digest("something won't be there");
  EXPECT_EQ(std::nullopt,
            CompilerRegistry::Instance()->TryGetCompilerPath(desc));
}

TEST(CompilerRegistry, Delete) {
  int size1, size2;
  auto now = flare::ReadSteadyClock();

  CompilerRegistry::Instance()->OnCompilerRescanTimer();
  size1 = QueryDynamicCompilers();

  FLARE_PCHECK(rename("./test-bin/1/bin/g++", "./test-bin/g++") == 0);
  CompilerRegistry::Instance()->OnCompilerRescanTimer();
  size2 = QueryDynamicCompilers();
  EXPECT_EQ(0, size1 - size2);

  FLARE_PCHECK(rename("./test-bin/g++", "./test-bin/tmp") == 0);
  CompilerRegistry::Instance()->OnCompilerRescanTimer();
  size2 = QueryDynamicCompilers();
  EXPECT_EQ(1, size1 - size2);

  FLARE_PCHECK(rename("./test-bin/tmp", "./test-bin/1/bin/g++") == 0);
}

TEST(CompilerRegistry, Stability) {
  flare::Fiber move = flare::Fiber([] {
    auto start_time = flare::ReadSteadyClock();
    while (flare::ReadSteadyClock() - start_time < 3s) {
      FLARE_PCHECK(rename("./test-bin/1/bin/g++", "./test-bin/temp") == 0);
      flare::this_fiber::SleepFor(100us);
      FLARE_PCHECK(rename("./test-bin/temp", "./test-bin/g++") == 0);
      flare::this_fiber::SleepFor(100us);
      FLARE_PCHECK(rename("./test-bin/g++", "./test-bin/1/bin/g++") == 0);
    }
  });
  flare::Fiber query = flare::Fiber([] {
    auto start_time = flare::ReadSteadyClock();
    while (flare::ReadSteadyClock() - start_time < 3s) {
      CompilerRegistry::Instance()->EnumerateEnvironments();
    }
  });
  flare::Fiber update = flare::Fiber([] {
    auto start_time = flare::ReadSteadyClock();
    while (flare::ReadSteadyClock() - start_time < 3s) {
      CompilerRegistry::Instance()->OnCompilerRescanTimer();
    }
  });
  move.join();
  query.join();
  update.join();
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
