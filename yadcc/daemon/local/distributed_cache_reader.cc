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

#include "yadcc/daemon/local/distributed_cache_reader.h"

#include <chrono>

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/xxhash/xxhash.h"

#include "flare/base/compression.h"
#include "flare/base/never_destroyed.h"
#include "flare/fiber/this_fiber.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/common_flags.h"

using namespace std::literals;

namespace yadcc::daemon::local {

flare::Decompressor* GetZstdDecompressor() {
  thread_local auto decompressor = flare::MakeDecompressor("zstd");
  return decompressor.get();
}

DistributedCacheReader* DistributedCacheReader::Instance() {
  static flare::NeverDestroyed<DistributedCacheReader> reader;
  return reader.Get();
}

DistributedCacheReader::DistributedCacheReader()
    : cache_bf_(1000, 100) /* Dummy filter, it will be overwritten soon. */
{
  if (!FLAGS_cache_server_uri.empty()) {
    cache_stub_ =
        std::make_unique<cache::CacheService_SyncStub>(FLAGS_cache_server_uri);

    LoadCacheBloomFilter();  // Fill the bloom filter immediately.
    last_bf_full_update_ = last_bf_update_ = flare::ReadCoarseSteadyClock();
    reload_bf_timer_ =
        flare::fiber::SetTimer(2s, [this] { LoadCacheBloomFilter(); });
  }
}

DistributedCacheReader::~DistributedCacheReader() {
  if (!FLAGS_cache_server_uri.empty()) {
    flare::fiber::KillTimer(reload_bf_timer_);
  }
}

std::optional<CacheEntry> DistributedCacheReader::TryRead(
    const std::string& key) {
  if (!cache_stub_) {  // Caching is not enabled at all.
    return std::nullopt;
  }

  {
    std::scoped_lock _(bf_lock_);
    if (last_bf_update_ + 10min >
            flare::ReadCoarseSteadyClock() /* It's still fresh (kind of). */
        && !cache_bf_.PossiblyContains(key)) {
      return std::nullopt;
    }
  }

  cache::TryGetEntryRequest req;
  req.set_token(FLAGS_token);
  req.set_key(key);

  flare::RpcClientController ctlr;
  ctlr.SetTimeout(10s);  // The response can be large.
  auto result = cache_stub_->TryGetEntry(req, &ctlr);
  if (!result) {
    // RPC failures are logged.
    FLARE_LOG_WARNING_IF(result.error().code() != cache::STATUS_NOT_FOUND,
                         "Failed to load cache: {}", result.error().ToString());

    // TODO(luobogao): Report cache miss rate if the error is
    // `STATUS_NOT_FOUND`. Given that we've checked our Bloom Filter, this
    // shouldn't be high, otherwise something is buggy.
    return std::nullopt;
  }

  auto parsed = TryParseCacheEntry(ctlr.GetResponseAttachment());
  if (!parsed) {
    FLARE_LOG_ERROR_EVERY_SECOND(
        "Unexpected: Compilation cache entry [{}] is found but it cannot be "
        "parsed.",
        key);
    return std::nullopt;
  }
  FLARE_VLOG(1, "Hit compilation cache entry [{}].", key);
  return parsed;
}

void DistributedCacheReader::Stop() {
  flare::fiber::KillTimer(reload_bf_timer_);
}

void DistributedCacheReader::Join() {
  // NOTHING.
}

void DistributedCacheReader::LoadCacheBloomFilter() {
  auto now = flare::ReadCoarseSteadyClock();
  cache::FetchBloomFilterRequest req;

  req.set_token(FLAGS_token);
  {
    std::scoped_lock _(bf_lock_);
    if (last_bf_full_update_.time_since_epoch() == 0s) {
      // We haven't succeeded yet, force a full update then.
      req.set_seconds_since_last_fetch(0x7fff'ffff);
      req.set_seconds_since_last_full_fetch(0x7fff'ffff);
    } else {
      req.set_seconds_since_last_fetch((now - last_bf_update_) / 1s);
      req.set_seconds_since_last_full_fetch((now - last_bf_full_update_) / 1s);
    }
  }

  flare::RpcClientController ctlr;
  ctlr.SetTimeout(10s);  // It can be large.
  auto result = cache_stub_->FetchBloomFilter(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING_EVERY_SECOND(
        "Failed to load compilation cache bloom filter from cache server: {}",
        result.error().ToString());
    return;
  }

  if (result->incremental()) {  // Incremental update.
    last_bf_update_ = now;

    for (auto&& e : result->newly_populated_keys()) {
      std::scoped_lock _(bf_lock_);
      cache_bf_.Add(e);
    }

    FLARE_VLOG(1, "Fetched {} newly populated cache entry keys.",
               result->newly_populated_keys().size());
  } else {  // Full update.
    last_bf_full_update_ = last_bf_update_ = now;

    auto decompressed =
        flare::Decompress(GetZstdDecompressor(), ctlr.GetResponseAttachment());
    if (!decompressed) {
      FLARE_LOG_ERROR_EVERY_SECOND(
          "Unexpected: Failed to decompress compilation cache bloom filter.");
      return;
    }

    auto bytes = flare::FlattenSlow(*decompressed);
    if ((bytes.size() & (bytes.size() - 1)) != 0) {
      FLARE_LOG_ERROR_EVERY_SECOND("Unexpected: Invalid bloom filter.");
      return;
    }

    std::scoped_lock _(bf_lock_);
    cache_bf_ =
        flare::experimental::SaltedBloomFilter(bytes, result->num_hashes());
  }
}

}  // namespace yadcc::daemon::local
