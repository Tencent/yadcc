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

#include "gtest/gtest.h"

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

TEST(TemporaryFile, Move) {
  TemporaryFile file("/tmp");
  EXPECT_TRUE(flare::StartsWith(file.GetPath(), "/tmp/"));
  EXPECT_NE(0, file.fd());

  auto file2 = std::move(file);
  EXPECT_TRUE(flare::StartsWith(file2.GetPath(), "/tmp/"));
  EXPECT_NE(0, file2.fd());
  EXPECT_TRUE(file.GetPath().empty());
  EXPECT_EQ(0, file.fd());

  TemporaryFile file3;
  EXPECT_TRUE(file3.GetPath().empty());
  EXPECT_EQ(0, file3.fd());
  file3 = std::move(file2);
  EXPECT_TRUE(flare::StartsWith(file3.GetPath(), "/tmp/"));
  EXPECT_NE(0, file3.fd());
  EXPECT_TRUE(file2.GetPath().empty());
  EXPECT_EQ(0, file2.fd());
}

TEST(TemporaryFile, Prefix) {
  TemporaryFile file("/dev/shm");

  EXPECT_TRUE(flare::StartsWith(file.GetPath(), "/dev/shm"));
  EXPECT_NE(0, file.fd());
  file.Write(flare::CreateBufferSlow("hello"));
  EXPECT_EQ("hello", flare::FlattenSlow(file.ReadAll()));
}

}  // namespace yadcc::daemon::cloud
