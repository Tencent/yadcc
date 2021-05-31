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

#ifndef YADCC_DAEMON_LOCAL_DISTRIBUTED_CACHE_READER_H_
#define YADCC_DAEMON_LOCAL_DISTRIBUTED_CACHE_READER_H_

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "flare/base/experimental/bloom_filter.h"

#include "yadcc/api/cache.flare.pb.h"
#include "yadcc/common/xxhash.h"
#include "yadcc/daemon/cache_format.h"

namespace yadcc::daemon::local {

// This class is responsible for reading from our compilation cache.
class DistributedCacheReader {
 public:
  static DistributedCacheReader* Instance();

  DistributedCacheReader();
  ~DistributedCacheReader();

  // Read from the cache.
  std::optional<CacheEntry> TryRead(const std::string& key);

  void Stop();
  void Join();

 private:
  void LoadCacheBloomFilter();

 private:
  std::unique_ptr<cache::CacheService_SyncStub> cache_stub_;

  std::uint64_t reload_bf_timer_;
  std::mutex bf_lock_;
  std::chrono::steady_clock::time_point last_bf_full_update_{};
  std::chrono::steady_clock::time_point last_bf_update_{};
  flare::experimental::SaltedBloomFilter cache_bf_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_DISTRIBUTED_CACHE_READER_H_
