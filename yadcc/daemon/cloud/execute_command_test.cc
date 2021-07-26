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

#include "yadcc/daemon/cloud/execute_command.h"

#include <unistd.h>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/logging.h"

#include "yadcc/common/io.h"

namespace yadcc::daemon::cloud {

TEST(ExecuteCommand, All) {
  int input[2], output[2], error[2];

  for (auto&& e : {input, output, error}) {
    FLARE_PCHECK(pipe(e) == 0);
    SetNonblocking(e[0]);
    SetNonblocking(e[1]);
  }

  auto pid = StartProgram("/bin/cat", 5, input[0], output[1], error[1], false);

  ASSERT_EQ(4, WriteTo(input[1], flare::CreateBufferSlow("1234")));
  ASSERT_EQ(4, WriteTo(input[1], flare::CreateBufferSlow("5678")));
  ASSERT_EQ(4, WriteTo(input[1], flare::CreateBufferSlow("90ab")));
  ASSERT_EQ(3, WriteTo(input[1], flare::CreateBufferSlow("cde")));
  ASSERT_EQ(1, WriteTo(input[1], flare::CreateBufferSlow("f")));

  FLARE_PCHECK(close(input[1]) == 0);
  FLARE_PCHECK(close(output[1]) == 0);
  FLARE_PCHECK(close(error[1]) == 0);

  int proc_status;
  FLARE_PCHECK(waitpid(pid, &proc_status, 0) == pid);
  ASSERT_TRUE(WIFEXITED(proc_status));
  EXPECT_EQ(0, WEXITSTATUS(proc_status));

  {
    flare::NoncontiguousBufferBuilder builder;
    ASSERT_EQ(ReadStatus::Eof, ReadAppend(output[0], &builder));
    EXPECT_EQ("1234567890abcdef", flare::FlattenSlow(builder.DestructiveGet()));
  }
  {
    flare::NoncontiguousBufferBuilder builder;
    ASSERT_EQ(ReadStatus::Eof, ReadAppend(error[0], &builder));
    EXPECT_EQ("", flare::FlattenSlow(builder.DestructiveGet()));
  }

  FLARE_PCHECK(close(input[0]) == 0);
  FLARE_PCHECK(close(output[0]) == 0);
  FLARE_PCHECK(close(error[0]) == 0);
}

}  // namespace yadcc::daemon::cloud
