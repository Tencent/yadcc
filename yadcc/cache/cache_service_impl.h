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

#ifndef YADCC_CACHE_CACHE_SERVICE_IMPL_H_
#define YADCC_CACHE_CACHE_SERVICE_IMPL_H_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flare/base/exposed_var.h"

#include "yadcc/api/cache.flare.pb.h"
#include "yadcc/cache/bloom_filter_generator.h"
#include "yadcc/cache/cache_engine.h"
#include "yadcc/cache/in_memory_cache.h"
#include "yadcc/common/token_verifier.h"

namespace yadcc::cache {

// Implements our cache server.
class CacheServiceImpl : public SyncCacheService {
 public:
  CacheServiceImpl();

  void FetchBloomFilter(const FetchBloomFilterRequest& request,
                        FetchBloomFilterResponse* response,
                        flare::RpcServerController* controller) override;
  void TryGetEntry(const TryGetEntryRequest& request,
                   TryGetEntryResponse* response,
                   flare::RpcServerController* controller) override;
  void PutEntry(const PutEntryRequest& request, PutEntryResponse* response,
                flare::RpcServerController* controller) override;

  // Initialize the service. This must be called prior to starting the server.
  void Start();

  // Shutdown the service.
  void Stop();
  void Join();

 private:
  std::vector<std::string> GetKeys() const;

  void OnRebuildTimer();

  // Dumps internals about the cache.
  Json::Value DumpInternals();

 private:
  std::unique_ptr<TokenVerifier> token_verifier_ = MakeTokenVerifierFromFlag();

  std::unique_ptr<CacheEngine> cache_;
  std::uint64_t cache_purge_timer_;

  std::unique_ptr<InMemoryCache> in_memory_cache_;

  std::mutex bf_lock_;
  BloomFilterGenerator bf_gen_;
  std::uint64_t bf_rebuild_timer_;

  flare::ExposedVarDynamic<Json::Value> internal_exposer_;
};

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_CACHE_SERVICE_IMPL_H_
