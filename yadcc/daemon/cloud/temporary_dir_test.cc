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

#include "yadcc/daemon/cloud/temporary_dir.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/logging.h"
#include "flare/base/string.h"

#include "yadcc/common/io.h"

namespace yadcc::daemon::cloud {

TEST(TemporaryDir, All) {
  TemporaryDir temp_dir("/tmp");
  std::string prefix = temp_dir.GetPath();
  EXPECT_TRUE(flare::StartsWith(prefix, "/tmp/"));
  std::map<std::string, std::string> data_map{{"test.a", "aaaaa"},
                                              {"test.b", "bbbbb"},
                                              {"test.c", "ccccc"},
                                              {"test.d", "ddddd"}};
  int fd = open((prefix + "/self").c_str(), O_WRONLY | O_CREAT, 0700);
  EXPECT_GE(fd, 0);
  WriteTo(fd, flare::CreateBufferSlow("ooooo"));
  close(fd);

  for (auto& [path, buf] : data_map) {
    int fd = open((prefix + "/" + path).c_str(), O_WRONLY | O_CREAT, 0700);
    EXPECT_GE(fd, 0);
    WriteTo(fd, flare::CreateBufferSlow(buf));
    close(fd);
  }

  {
    FLARE_PCHECK(mkdir((prefix + "/subdir").c_str(), 0755) == 0);
    std::ofstream ofs(prefix + "/subdir/my-file");
    ofs << "my data";
  }

  TemporaryDir temp_dir2;
  temp_dir2 = std::move(temp_dir);

  auto buffers = temp_dir2.ReadAll();
  EXPECT_EQ(6, buffers.size());
  for (auto&& [path, buf] : buffers) {
    std::cout << "Reading from " << path << "\n";
    if (path == "self") {
      EXPECT_EQ("ooooo", flare::FlattenSlow(buf));
    } else if (path == "subdir/my-file") {
      EXPECT_EQ("my data", flare::FlattenSlow(buf));
    } else {
      EXPECT_EQ(data_map[path], flare::FlattenSlow(buf));
    }
  }
}

}  // namespace yadcc::daemon::cloud
