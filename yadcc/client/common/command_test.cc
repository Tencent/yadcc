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

#include "yadcc/client/common/command.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace yadcc::client {

TEST(ExecuteCommand, Case1) {
  auto result = ExecuteCommand(RewrittenArgs("/bin/cat", {}), "12341234");
  EXPECT_EQ(0, result.exit_code);
  EXPECT_EQ("12341234", result.output);
  EXPECT_TRUE(result.error.empty());
}

TEST(ExecuteCommand, Case2) {
  TransparentOutputStream os;
  std::string err;
  auto ec =
      ExecuteCommand(RewrittenArgs("/bin/cat", {}), {}, "123456", &os, &err);

  EXPECT_EQ(0, ec);
  EXPECT_EQ("123456", os.Get());
  EXPECT_TRUE(err.empty());
}

TEST(PassthroughToProgram, ExitCode) {
  const char* argv[] = {nullptr};
  EXPECT_EQ(0, PassthroughToProgram("/bin/true", argv));
}

TEST(ExecuteCommand, Mock) {
  SetExecuteCommandHandler(
      [](auto&&...) { return ExecutionResult{.exit_code = -1}; });
  EXPECT_EQ(-1, ExecuteCommand(RewrittenArgs("/bin/cat", {})).exit_code);
  SetExecuteCommandHandler(nullptr);
}

}  // namespace yadcc::client
