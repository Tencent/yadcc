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

#include "yadcc/cache/bloom_filter_generator.h"

#include <chrono>

#include "gtest/gtest.h"

using namespace std::literals;

namespace yadcc::cache {

TEST(BloomFilterGenerator, All) {
  BloomFilterGenerator gen;

  {
    auto empty = gen.GetBloomFilter();
    EXPECT_FALSE(empty.PossiblyContains("a"));
    EXPECT_FALSE(empty.PossiblyContains("b"));
    EXPECT_FALSE(empty.PossiblyContains("c"));
  }

  gen.Rebuild({"a", "b", "c"}, 1s);
  {
    auto current = gen.GetBloomFilter();
    EXPECT_TRUE(current.PossiblyContains("a"));
    EXPECT_TRUE(current.PossiblyContains("b"));
    EXPECT_TRUE(current.PossiblyContains("c"));
    EXPECT_FALSE(current.PossiblyContains("d"));
  }
  gen.Add("d");

  {
    auto current = gen.GetBloomFilter();
    EXPECT_TRUE(current.PossiblyContains("a"));
    EXPECT_TRUE(current.PossiblyContains("b"));
    EXPECT_TRUE(current.PossiblyContains("c"));
    EXPECT_TRUE(current.PossiblyContains("d"));
    EXPECT_FALSE(current.PossiblyContains("e"));
  }

  std::this_thread::sleep_for(2s);
  gen.Add("1");
  gen.Rebuild({"2", "3", "4"}, 1s);

  {
    auto current = gen.GetBloomFilter();
    EXPECT_TRUE(current.PossiblyContains("1"));
    EXPECT_TRUE(current.PossiblyContains("2"));
    EXPECT_TRUE(current.PossiblyContains("3"));
    EXPECT_TRUE(current.PossiblyContains("4"));
    EXPECT_FALSE(current.PossiblyContains("5"));
  }

  gen.Rebuild({"2", "3", "4"}, 0s);

  {
    auto current = gen.GetBloomFilter();
    EXPECT_TRUE(current.PossiblyContains("2"));
    EXPECT_TRUE(current.PossiblyContains("3"));
    EXPECT_TRUE(current.PossiblyContains("4"));
    EXPECT_FALSE(current.PossiblyContains("5"));
  }
}

}  // namespace yadcc::cache
