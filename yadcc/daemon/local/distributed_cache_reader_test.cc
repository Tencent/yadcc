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

#include "gtest/gtest.h"
#include "xxhash/xxhash.h"

#include "flare/base/compression.h"
#include "flare/base/experimental/bloom_filter.h"
#include "flare/fiber/future.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/rpc_server_controller.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_mock.h"

#include "yadcc/api/cache.flare.pb.h"
#include "yadcc/daemon/cache_format.h"

using namespace std::literals;

FLARE_OVERRIDE_FLAG(cache_server_uri, "mock://what-ever-it-wants-to-be");

namespace yadcc::daemon::local {

namespace {

struct XxHash {
  std::size_t operator()(const std::string_view& str) const noexcept {
    return XXH64(str.data(), str.size(), 0);
  }
};

void HandleFetchBloomFilter(const cache::FetchBloomFilterRequest& req,
                            cache::FetchBloomFilterResponse* resp,
                            flare::RpcServerController* ctlr) {
  // The first call.
  if (req.seconds_since_last_full_fetch() == 0x7fff'ffff) {
    flare::experimental::SaltedBloomFilter bf(12345678, 3);

    bf.Add("my cache key1");
    bf.Add("my cache key2");
    bf.Add("my cache key3");

    resp->set_incremental(false);
    resp->set_num_hashes(bf.GetIterationCount());
    ctlr->SetResponseAttachment(
        *flare::Compress(&*flare::MakeCompressor("zstd"), bf.GetBytes()));
  } else {
    resp->set_incremental(true);
    resp->add_newly_populated_keys("my cache key4");
  }
}

void HandleTryGetEntry(const cache::TryGetEntryRequest& request,
                       cache::TryGetEntryResponse* response,
                       flare::RpcServerController* controller) {
  controller->SetResponseAttachment(
      WriteCacheEntry(CacheEntry{.exit_code = 123,
                                 .standard_output = "output",
                                 .standard_error = "err",
                                 .files = flare::CreateBufferSlow("obj")}));
}

}  // namespace

TEST(DistributedCacheReader, Success) {
  FLARE_EXPECT_RPC(cache::CacheService::FetchBloomFilter, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(HandleFetchBloomFilter));

  FLARE_EXPECT_RPC(cache::CacheService::TryGetEntry, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(HandleTryGetEntry));

  auto result = DistributedCacheReader::Instance()->TryRead("my cache key1");
  ASSERT_TRUE(result);
  EXPECT_EQ(123, result->exit_code);
  EXPECT_EQ("output", result->standard_output);
  EXPECT_EQ("err", result->standard_error);
  EXPECT_EQ("obj", flare::FlattenSlow(result->files));

  // Called before incremental bloom filter update happens.
  result = DistributedCacheReader::Instance()->TryRead("my cache key4");
  EXPECT_FALSE(result);

  std::this_thread::sleep_for(5s);  // Wait for incremental bloom filter update.
  result = DistributedCacheReader::Instance()->TryRead("my cache key4");
  EXPECT_TRUE(result);
}

TEST(DistributedCacheReader, BloomFilterMiss) {
  FLARE_EXPECT_RPC(cache::CacheService::TryGetEntry, ::testing::_).Times(0);
  EXPECT_FALSE(DistributedCacheReader::Instance()->TryRead("my cache key5"));
}

TEST(DistributedCacheReader, Failure) {
  FLARE_EXPECT_RPC(cache::CacheService::TryGetEntry, ::testing::_)
      .WillRepeatedly(flare::testing::Return(flare::rpc::STATUS_TIMEOUT, ""));
  EXPECT_FALSE(DistributedCacheReader::Instance()->TryRead("my cache key"));
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
