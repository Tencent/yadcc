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

#include "yadcc/cache/consistent_hash.h"

#include <algorithm>
#include <cstdint>
#include <iostream>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/random.h"

#include "yadcc/common/xxhash.h"

namespace yadcc::cache {

class ConsistentHashTest : public testing::Test {
 protected:
  virtual void SetUp() {
    weighted_dirs_ = {{"/yadcc/0", 1},
                      {"/yadcc/1", 2},
                      {"/yadcc/2", 3},
                      {"/yadcc/3", 4},
                      {"/yadcc/4", 5}};
    weighted_dirs_uniform_ = {{"/yadcc/0", 1},
                              {"/yadcc/1", 1},
                              {"/yadcc/2", 1},
                              {"/yadcc/3", 1},
                              {"/yadcc/4", 1}};
    weighted_dirs_add_one_ = {{"/yadcc/0", 1}, {"/yadcc/1", 1},
                              {"/yadcc/2", 1}, {"/yadcc/3", 1},
                              {"/yadcc/4", 1}, {"/yadcc/5", 1}};
    weighted_dirs_remove_one_ = {
        {"/yadcc/0", 1}, {"/yadcc/1", 1}, {"/yadcc/2", 1}, {"/yadcc/3", 1}};
  }

  std::map<std::string, std::uint64_t> weighted_dirs_uniform_;
  std::map<std::string, std::uint64_t> weighted_dirs_add_one_;
  std::map<std::string, std::uint64_t> weighted_dirs_;
  std::map<std::string, std::uint64_t> weighted_dirs_remove_one_;
};

TEST_F(ConsistentHashTest, Uniform) {
  ConsistentHash consistentHash(weighted_dirs_uniform_, XxHash());
  std::map<std::string, std::size_t> hash_counter;
  auto total_weight = std::accumulate(
      weighted_dirs_uniform_.begin(), weighted_dirs_uniform_.end(), 0u,
      [](std::uint32_t l,
         const std::map<std::string, std::uint32_t>::value_type& r) {
        return l + r.second;
      });
  for (int i = 0; i < 100000; ++i) {
    auto random_key = flare::Random();
    auto&& node = consistentHash.GetNode(random_key);
    EXPECT_NE(weighted_dirs_uniform_.count(node), 0);
    hash_counter[node]++;
  }
  for (auto& [dir, count] : hash_counter) {
    std::cout << dir << ":" << count << std::endl;
    EXPECT_NEAR(
        static_cast<double>(count) / 100000,
        static_cast<double>(weighted_dirs_uniform_.at(dir)) / total_weight,
        0.05);
  }
}

TEST_F(ConsistentHashTest, Weighted) {
  ConsistentHash consistentHash(weighted_dirs_, XxHash());
  std::map<std::string, std::size_t> hash_counter;
  auto total_weight = std::accumulate(
      weighted_dirs_.begin(), weighted_dirs_.end(), 0u,
      [](std::uint32_t l,
         const std::map<std::string, std::uint32_t>::value_type& r) {
        return l + r.second;
      });
  for (int i = 0; i < 100000; ++i) {
    auto random_key = flare::Random();
    auto&& node = consistentHash.GetNode(random_key);
    EXPECT_NE(weighted_dirs_.count(node), 0);
    hash_counter[node]++;
  }
  for (auto& [dir, count] : hash_counter) {
    std::cout << dir << ":" << count << std::endl;
    EXPECT_NEAR(static_cast<double>(count) / 100000,
                static_cast<double>(weighted_dirs_.at(dir)) / total_weight,
                0.049);
  }
}

TEST_F(ConsistentHashTest, NodeAdded) {
  ConsistentHash consistentHash(weighted_dirs_uniform_, XxHash());
  ConsistentHash consistentHashAdd(weighted_dirs_add_one_, XxHash());

  auto total = 0, mismatch = 0;
  for (int i = 0; i < 100000; ++i, ++total) {
    auto random_key = flare::Random();
    if (consistentHash.GetNode(random_key) !=
        consistentHashAdd.GetNode(random_key)) {
      ++mismatch;
    }
  }

  EXPECT_LE(static_cast<double>(mismatch) / total,
            1.0 / weighted_dirs_uniform_.size());

  std::cout << "total:" << total << ", mismatch:" << mismatch << std::endl;
}

TEST_F(ConsistentHashTest, NodeRemoved) {
  ConsistentHash consistentHash(weighted_dirs_uniform_, XxHash());
  ConsistentHash consistentHashRemove(weighted_dirs_remove_one_, XxHash());

  auto total = 0, mismatch = 0;
  for (int i = 0; i < 100000; ++i, ++total) {
    auto random_key = flare::Random();
    if (consistentHash.GetNode(random_key) !=
        consistentHashRemove.GetNode(random_key)) {
      ++mismatch;
    }
  }

  EXPECT_LE(static_cast<double>(mismatch) / total,
            1.0 / weighted_dirs_uniform_.size());

  std::cout << "total:" << total << ", mismatch:" << mismatch << std::endl;
}

}  // namespace yadcc::cache
