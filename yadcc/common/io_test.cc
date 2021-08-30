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

#include "yadcc/common/io.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/logging.h"

namespace yadcc {

TEST(Io, All) {
  int fds[2];

  FLARE_PCHECK(pipe(fds) == 0);
  SetNonblocking(fds[0]);
  SetNonblocking(fds[1]);

  flare::NoncontiguousBufferBuilder builder;

  EXPECT_EQ(ReadStatus::TryAgainLater, ReadAppend(fds[0], &builder));
  ASSERT_EQ(5, WriteTo(fds[1], flare::CreateBufferSlow("hello")));
  EXPECT_EQ(ReadStatus::TryAgainLater, ReadAppend(fds[0], &builder));
  FLARE_PCHECK(close(fds[1]) == 0);
  EXPECT_EQ(ReadStatus::Eof, ReadAppend(fds[0], &builder));
  FLARE_PCHECK(close(fds[0]) == 0);

  EXPECT_EQ("hello", flare::FlattenSlow(builder.DestructiveGet()));

  WriteAll("./test_write_all", flare::CreateBufferSlow("my test data"));
  int fd = open("./test_write_all", O_RDONLY);
  EXPECT_GT(fd, 0);
  flare::NoncontiguousBufferBuilder builder2;
  ReadAppend(fd, &builder2);
  EXPECT_EQ("my test data", flare::FlattenSlow(builder2.DestructiveGet()));
  close(fd);
}

}  // namespace yadcc
