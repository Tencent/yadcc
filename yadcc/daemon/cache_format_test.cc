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

#include "yadcc/daemon/cache_format.h"

#include "gtest/gtest.h"

namespace yadcc::daemon {

TEST(CacheFormat, All) {
  CacheEntry result = {
      .exit_code = 1,
      .standard_output = "output",
      .standard_error = "error",
      .files = flare::CreateBufferSlow(std::string(123456, 'a'))};

  auto bytes = WriteCacheEntry(result);
  ASSERT_FALSE(bytes.Empty());

  auto parsed = TryParseCacheEntry(bytes);
  ASSERT_TRUE(parsed);

  EXPECT_EQ(1, parsed->exit_code);
  EXPECT_EQ("output", parsed->standard_output);
  EXPECT_EQ("error", parsed->standard_error);

  // Not using `EXPECT_EQ` because on failure `EXPECT_EQ` would print a huge
  // amount of diagnostics.
  EXPECT_TRUE(std::string(123456, 'a') == flare::FlattenSlow(parsed->files));
}

TEST(CacheFormat, Corrupted) {
  CacheEntry result = {.files =
                           flare::CreateBufferSlow(std::string(123456, 'a'))};

  auto bytes = WriteCacheEntry(result);
  ASSERT_FALSE(bytes.Empty());

  // Modified without updating digest, a corrupted entry.
  auto str = flare::FlattenSlow(bytes);
  ++str.back();
  bytes = flare::CreateBufferSlow(str);

  EXPECT_FALSE(TryParseCacheEntry(bytes));
}

TEST(CacheFormat, Corrupted2) {
  CacheEntry result = {.files =
                           flare::CreateBufferSlow(std::string(123456, 'a'))};

  auto bytes = WriteCacheEntry(result);
  ASSERT_FALSE(bytes.Empty());

  auto str = flare::FlattenSlow(bytes);
  str.pop_back();  // Missing bytes at the back.
  bytes = flare::CreateBufferSlow(str);

  EXPECT_FALSE(TryParseCacheEntry(bytes));
}

TEST(CacheFormat, Corrupted3) {
  CacheEntry result = {.files =
                           flare::CreateBufferSlow(std::string(123456, 'a'))};
  auto bytes = WriteCacheEntry(result);
  ASSERT_FALSE(bytes.Empty());

  bytes.Skip(1);  // Missing bytes at beginning.
  EXPECT_FALSE(TryParseCacheEntry(bytes));
}

TEST(CacheFormat, CorruptedEmpty) {
  auto bytes = flare::CreateBufferSlow("");
  EXPECT_FALSE(TryParseCacheEntry(bytes));
}

}  // namespace yadcc::daemon
