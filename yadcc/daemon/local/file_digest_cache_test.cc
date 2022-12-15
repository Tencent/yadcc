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

#include "yadcc/daemon/local/file_digest_cache.h"

#include "gtest/gtest.h"

namespace yadcc::daemon::local {

TEST(FileDigestCache, All) {
  FileDigestCache cache;

  EXPECT_FALSE(cache.TryGet("/path/to/something", 10, 1111));
  cache.Set("/path/to/something", 10, 1111, "my hash");
  auto result = cache.TryGet("/path/to/something", 10, 1111);
  ASSERT_TRUE(result);
  EXPECT_EQ("my hash", *result);
  EXPECT_FALSE(cache.TryGet("/path/to/something", 10, 1112));
  EXPECT_FALSE(cache.TryGet("/path/to/something", 11, 1111));
  EXPECT_FALSE(cache.TryGet("/path/to/something2", 10, 1111));

  auto jsv = cache.DumpInternals();
  ASSERT_EQ(1, jsv.size());
  EXPECT_EQ("my hash", jsv["/path/to/something"]["digest"].asString());
}

}  // namespace yadcc::daemon::local
