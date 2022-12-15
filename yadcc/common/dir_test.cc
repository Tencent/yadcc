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

#include "yadcc/common/dir.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <fstream>
#include <iostream>

#include "gtest/gtest.h"

#include "flare/base/logging.h"

namespace yadcc::cache {

TEST(Dir, EnumerateDir) {
  FLARE_PCHECK(mkdir("./clean", 0755) == 0);
  FLARE_PCHECK(mkdir("./clean/test-dir", 0755) == 0);
  FLARE_PCHECK(mkdir("./clean/test-dir-2", 0755) == 0);

  {
    std::fstream fs("./clean/file", std::fstream::out);
    fs << "body";
    fs.close();
  }

  auto result = EnumerateDir("./clean");
  for (auto&& e : result) {
    std::cout << "Found " << e.name << std::endl;
  }

  ASSERT_EQ(3, result.size());
  std::sort(result.begin(), result.end(),
            [&](auto&& x, auto&& y) { return x.name < y.name; });

  EXPECT_EQ("file", result[0].name);
  EXPECT_TRUE(result[0].is_regular);
  EXPECT_FALSE(result[0].is_dir);
  EXPECT_EQ("test-dir", result[1].name);
  EXPECT_FALSE(result[1].is_regular);
  EXPECT_TRUE(result[1].is_dir);
  EXPECT_EQ("test-dir-2", result[2].name);
  EXPECT_FALSE(result[2].is_regular);
  EXPECT_TRUE(result[2].is_dir);
}

TEST(Dir, EnumerateDirRecursively) {
  FLARE_PCHECK(mkdir("./clean-recursive", 0755) == 0);
  FLARE_PCHECK(mkdir("./clean-recursive/test-dir", 0755) == 0);
  FLARE_PCHECK(mkdir("./clean-recursive/test-dir-2", 0755) == 0);

  {
    std::fstream fs("./clean-recursive/test-dir-2/file", std::fstream::out);
    fs << "body";
    fs.close();
  }

  auto result = EnumerateDirRecursively("./clean-recursive");
  std::sort(result.begin(), result.end(),
            [&](auto&& x, auto&& y) { return x.name < y.name; });
  for (auto&& e : result) {
    std::cout << "Found " << e.name << std::endl;
  }

  ASSERT_EQ(3, result.size());

  EXPECT_EQ("test-dir", result[0].name);
  EXPECT_FALSE(result[0].is_regular);
  EXPECT_TRUE(result[0].is_dir);
  EXPECT_EQ("test-dir-2", result[1].name);
  EXPECT_FALSE(result[1].is_regular);
  EXPECT_TRUE(result[1].is_dir);
  EXPECT_EQ("test-dir-2/file", result[2].name);
  EXPECT_TRUE(result[2].is_regular);
  EXPECT_FALSE(result[2].is_dir);
}

TEST(Dir, Mkdirs) {
  Mkdirs("./clean2/test-dir");
  Mkdirs("./clean2/test-dir2");
  Mkdirs("./clean2/test-dir2");
  Mkdirs("./clean2/test-dir2/3");
  Mkdirs("./clean2/test-dir2/4");
  Mkdirs("./clean2/test-dir2/4/5");
  EXPECT_EQ(2, EnumerateDir("./clean2").size());
  EXPECT_EQ(2, EnumerateDir("./clean2/test-dir2").size());
  EXPECT_EQ(1, EnumerateDir("./clean2/test-dir2/4").size());
}

TEST(Dir, RemoveDirs) {
  Mkdirs("./clean3/test-dir/a");
  Mkdirs("./clean3/test-dir/a");
  Mkdirs("./clean3/test-dir/a");
  Mkdirs("./clean3/test-dir/ab");
  Mkdirs("./clean3/test-dir/abc");
  Mkdirs("./clean3/test-dir/abcd");
  EXPECT_EQ(4, EnumerateDir("./clean3/test-dir").size());
  RemoveDirs("./clean3/test-dir");
  EXPECT_TRUE(EnumerateDir("./clean3").empty());
}

TEST(Dir, GetCanonicalPath) {
  EXPECT_EQ("/dev/null", GetCanonicalPath("/dev/../../../../dev/null"));
}

}  // namespace yadcc::cache
