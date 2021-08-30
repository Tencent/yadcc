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

#include "yadcc/cache/cache_service_impl.h"

#include <chrono>
#include <memory>
#include <string>

#include "gflags/gflags.h"

#include "flare/base/compression.h"
#include "flare/base/random.h"
#include "flare/base/string.h"

#include "flare/fiber/timer.h"
#include "flare/rpc/logging.h"
#include "flare/rpc/rpc_server_controller.h"

#include "yadcc/common/parse_size.h"
#include "yadcc/common/token_verifier.h"

using namespace std::literals;

DEFINE_string(cache_engine, "disk",
              "Choose which cache engine we decide to use. `disk` is the only "
              "option we support currently.");

DEFINE_string(max_in_memory_cache_size, "4G",
              "This option control the max in-memory size we can use. `4G` is "
              "the default value.");

namespace yadcc::cache {

namespace {

// Returns after how long should full (instead of an incremental one) Bloom
// Filter be returned to the client.
//
// To avoid burst full fetch (which can be bandwidth-consuming), we set a
// slightly different interval for each client.
std::chrono::nanoseconds GetBloomFilterFullFetchIntervalFor(
    const flare::Endpoint& client) {
  constexpr auto kBaseDelay = 10min;
  constexpr auto kMaxPerClientBias = 120;  // Seconds.
  constexpr auto kMaxRandomDelay = 120;    // Seconds.

  // Up to `kBaseDelay + (kMaxPerClientBias + kMaxRandomDelay) * 1s`.
  return kBaseDelay +
         // Per-client "random" delay.
         std::hash<std::string>{}(client.ToString()) % kMaxPerClientBias * 1s +
         // And some random delay.
         flare::Random(kMaxRandomDelay) * 1s;
}

}  // namespace

CacheServiceImpl::CacheServiceImpl()
    : internal_exposer_("yadcc/cache", [this] { return DumpInternals(); }) {
  // Timers are started in `Start()`.
  auto max_size = TryParseSize(FLAGS_max_in_memory_cache_size);
  FLARE_CHECK(max_size, "Flag max_in_memory_cache_size is invalid.");
  is_user_verifier_ = MakeTokenVerifierFromFlag(FLAGS_acceptable_user_tokens);
  is_servant_verifier_ =
      MakeTokenVerifierFromFlag(FLAGS_acceptable_servant_tokens);
  cache_ = cache_engine_registry.New(FLAGS_cache_engine);
  in_memory_cache_ = std::make_unique<InMemoryCache>(*max_size);
}

void CacheServiceImpl::FetchBloomFilter(
    const FetchBloomFilterRequest& request, FetchBloomFilterResponse* response,
    flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  if (!is_user_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  if (request.seconds_since_last_fetch() >
      request.seconds_since_last_full_fetch()) {
    controller->SetFailed(STATUS_INVALID_ARGUMENT);
    return;
  }

  // We need to keep frequency of full update low, it's bandwidth-consuming.
  //
  // FIXME: If there are way too many newly populated keys during last 10
  // minutes, returning a full bloom filter may be more bandwidth efficient. We
  // might want to set a threshold and return the full Bloom Filter if there are
  // too many new keys.
  response->set_incremental(
      request.seconds_since_last_full_fetch() <
      GetBloomFilterFullFetchIntervalFor(controller->GetRemotePeer()) / 1s);
  if (response->incremental()) {
    constexpr auto kNetworkDelayCompensation = 5s;
    // It's fresh enough, let it update its Bloom Filter incrementally.
    for (auto&& e :
         bf_gen_.GetNewlyPopulatedKeys(request.seconds_since_last_fetch() * 1s +
                                       kNetworkDelayCompensation)) {
      response->add_newly_populated_keys(e);
    }
  } else {
    // Return the full Bloom Filter then.
    auto filter = bf_gen_.GetBloomFilter();
    auto compressed_bytes =
        flare::Compress(flare::MakeCompressor("zstd").get(), filter.GetBytes());
    FLARE_CHECK(compressed_bytes);  // How can compression fail?
    response->set_num_hashes(filter.GetIterationCount());
    controller->SetResponseAttachment(*compressed_bytes);
  }
}

void CacheServiceImpl::TryGetEntry(const TryGetEntryRequest& request,
                                   TryGetEntryResponse* response,
                                   flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  if (!is_user_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  auto bytes = in_memory_cache_->TryGet(request.key());
  if (!bytes) {
    bytes = cache_->TryGet(request.key());  // Try L2 then.
  }
  if (!bytes) {
    cache_miss_.fetch_add(1, std::memory_order_relaxed);
    controller->SetFailed(STATUS_NOT_FOUND, "Cache miss.");
    return;
  }

  cache_hits_.fetch_add(1, std::memory_order_relaxed);
  in_memory_cache_->Put(request.key(), *bytes);
  controller->SetResponseAttachment(*bytes);
}

void CacheServiceImpl::PutEntry(const PutEntryRequest& request,
                                PutEntryResponse* response,
                                flare::RpcServerController* controller) {
  flare::AddLoggingItemToRpc(controller->GetRemotePeer().ToString());

  if (!is_servant_verifier_->Verify(request.token())) {
    controller->SetFailed(STATUS_ACCESS_DENIED);
    return;
  }

  auto&& key = request.key();
  auto&& body = controller->GetRequestAttachment();

  // For better auditability.
  FLARE_LOG_INFO("Filled cache entry [{}] with {} bytes.", key,
                 body.ByteSize());

  cache_->Put(key, body);
  in_memory_cache_->Put(key, body);
  bf_gen_.Add(key);
}

void CacheServiceImpl::Start() {
  // They're heavy operation, so don't to it too frequently.
  cache_purge_timer_ =
      flare::fiber::SetTimer(1min, [this] { cache_->Purge(); });
  bf_rebuild_timer_ = flare::fiber::SetTimer(60s, [this] { OnRebuildTimer(); });

  // Make sure the Bloom Filter is ready before we start serving the clients.
  bf_gen_.Rebuild(GetKeys(), 0s /* Not applicable. */);
}

void CacheServiceImpl::Stop() {
  flare::fiber::KillTimer(cache_purge_timer_);
  flare::fiber::KillTimer(bf_rebuild_timer_);
}

void CacheServiceImpl::Join() {
  // NOTHING.
}

// Bloom filter can deal with the duplicate keys case. We can get the advantage
// of fast insertions.
std::vector<std::string> CacheServiceImpl::GetKeys() const {
  std::vector<std::string> keys;
  for (auto&& k : in_memory_cache_->GetKeys()) {
    keys.emplace_back(std::move(k));
  }
  for (auto&& k : cache_->GetKeys()) {
    keys.emplace_back(std::move(k));
  }
  return keys;
}

void CacheServiceImpl::OnRebuildTimer() {
  auto keys = GetKeys();
  bf_gen_.Rebuild(keys, 10s /* Arbitrarily chosen. */);
}

Json::Value CacheServiceImpl::DumpInternals() {
  Json::Value jsv;
  jsv["l1"] = in_memory_cache_->DumpInternals();
  jsv["l2"] = cache_->DumpInternals();
  jsv["hits"] =
      static_cast<Json::UInt64>(cache_hits_.load(std::memory_order_relaxed));
  jsv["misses"] =
      static_cast<Json::UInt64>(cache_miss_.load(std::memory_order_relaxed));
  return jsv;
}

}  // namespace yadcc::cache
