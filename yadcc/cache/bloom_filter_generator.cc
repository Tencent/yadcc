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
#include <string>
#include <vector>

#include "flare/base/chrono.h"

namespace yadcc::cache {

void BloomFilterGenerator::Rebuild(
    const std::vector<std::string>& keys,
    std::chrono::seconds key_generation_compensation) {
  std::scoped_lock _(lock_);
  auto compensation = UnsafeGetNewlyPopulatedKeys(key_generation_compensation);

  // Rebuild a brand new bloom filter.
  flare::experimental::SaltedBloomFilter filter(kBloomFilterSizeInBits,
                                                kHashIterationCount);
  for (auto&& e : keys) {
    filter.Add(e);
  }
  for (auto&& e : compensation) {
    filter.Add(e);
  }
  current_bf_ = std::move(filter);
}

void BloomFilterGenerator::Add(const std::string& cache_key) {
  std::scoped_lock _(lock_);
  current_bf_.Add(cache_key);
  newly_populated_keys_.emplace_back(cache_key, flare::ReadCoarseSteadyClock());
}

std::vector<std::string> BloomFilterGenerator::GetNewlyPopulatedKeys(
    std::chrono::nanoseconds recent) {
  std::scoped_lock _(lock_);
  return UnsafeGetNewlyPopulatedKeys(recent);
}

flare::experimental::SaltedBloomFilter BloomFilterGenerator::GetBloomFilter()
    const {
  std::scoped_lock _(lock_);
  return current_bf_;
}

std::vector<std::string> BloomFilterGenerator::UnsafeGetNewlyPopulatedKeys(
    std::chrono::nanoseconds recent) {
  auto since = flare::ReadCoarseSteadyClock() - recent;
  auto keep_since = flare::ReadCoarseSteadyClock() - kNewlyPopulatedKeyHistory;

  // Drop old entries.
  while (!newly_populated_keys_.empty() &&
         newly_populated_keys_.front().second < keep_since) {
    newly_populated_keys_.pop_front();
  }

  std::vector<std::string> result;
  for (auto iter = newly_populated_keys_.rbegin();
       iter != newly_populated_keys_.rend(); ++iter) {
    if (iter->second < since) {
      break;
    }
    result.push_back(iter->first);
  }
  return result;
}

}  // namespace yadcc::cache
