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

#include "yadcc/daemon/cloud/distributed_cache_writer.h"

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/fiber/future.h"
#include "flare/init/override_flag.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_mock.h"

#include "yadcc/api/cache.flare.pb.h"

FLARE_OVERRIDE_FLAG(cache_server_uri, "mock://what-ever-it-wants-to-be");

namespace yadcc::daemon::cloud {

void HandlePutEntrySuccess(const cache::PutEntryRequest& request,
                           cache::PutEntryResponse* response,
                           flare::RpcServerController* controller) {
  // Nothing special.
}

void HandlePutEntryFailed(const cache::PutEntryRequest& request,
                          cache::PutEntryResponse* response,
                          flare::RpcServerController* controller) {
  controller->SetFailed("failed");
}

TEST(DistributedCacheWriter, Success) {
  FLARE_EXPECT_RPC(cache::CacheService::PutEntry, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(HandlePutEntrySuccess));

  EXPECT_TRUE(
      flare::fiber::BlockingGet(DistributedCacheWriter::Instance()->AsyncWrite(
          "my cache key", 0, "output", "error",
          flare::CreateBufferSlow("object file"))));
}

TEST(DistributedCacheWriter, Failure) {
  FLARE_EXPECT_RPC(cache::CacheService::PutEntry, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(HandlePutEntryFailed));

  EXPECT_FALSE(
      flare::fiber::BlockingGet(DistributedCacheWriter::Instance()->AsyncWrite(
          "my cache key", 0, {}, {}, flare::CreateBufferSlow("object file"))));
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
