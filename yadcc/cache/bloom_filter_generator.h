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

#ifndef YADCC_CACHE_BLOOM_FILTER_GENERATOR_H_
#define YADCC_CACHE_BLOOM_FILTER_GENERATOR_H_

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "flare/base/buffer.h"
#include "flare/base/experimental/bloom_filter.h"

#include "yadcc/common/xxhash.h"

namespace yadcc::cache {

// This class helps us to generate a Bloom Filter that (approximately) reflects
// the cache entries we have.
//
// Thread-safe.
class BloomFilterGenerator {
 public:
  // Rebuild internal state of the generator from keys in our cache.
  //
  // Due to implementation limitations, generating `keys` costs time. So as not
  // to lose keys newly added during generating `keys`, internally we treat keys
  // newly `Add`-ed to us in last `key_generation_compensation` seconds as
  // existing (even if they're not in `keys`).
  void Rebuild(const std::vector<std::string>& keys,
               std::chrono::seconds key_generation_compensation);

  // Notifies the generator that a new key is populated.
  void Add(const std::string& cache_key);

  // Get keys newly-added to this object in `recent` time period.
  //
  // Internally we only store a history of 1h.
  std::vector<std::string> GetNewlyPopulatedKeys(
      std::chrono::nanoseconds recent);

  // Returns a (nearly) up-to-date bloom filter.
  flare::experimental::SaltedBloomFilter GetBloomFilter() const;

 private:
  std::vector<std::string> UnsafeGetNewlyPopulatedKeys(
      std::chrono::nanoseconds recent);

 private:
  // Number of hash values generated for each key.
  inline static constexpr auto kHashIterationCount = 10;

  // @sa: https://hur.st/bloomfilter/?n=1048576&p=0.00001&m=&k=10
  inline static constexpr auto kBloomFilterSizeInBits = 27584639;  // ~4MB.

  // How long history of newly-added keys should we keep.
  inline static constexpr auto kNewlyPopulatedKeyHistory =
      std::chrono::hours(1);

  mutable std::mutex lock_;

  // Filled by `Build()`.
  flare::experimental::SaltedBloomFilter current_bf_{kBloomFilterSizeInBits,
                                                     kHashIterationCount};

  // Keeps newly-populated keys during last hour.
  std::deque<std::pair<std::string, std::chrono::steady_clock::time_point>>
      newly_populated_keys_;
};

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_BLOOM_FILTER_GENERATOR_H_
