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

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/fiber/execution_context.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/rpc_server_controller.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_controller.h"

#include "yadcc/api/cache.flare.pb.h"

using namespace std::literals;

FLARE_OVERRIDE_FLAG(acceptable_tokens, "token1,token2");

namespace yadcc::cache {

TEST(CacheServiceImpl, Token) {
  flare::fiber::ExecutionContext::Create()->Execute([&] {
    CacheServiceImpl impl;
    flare::RpcServerController ctlr;

    {
      TryGetEntryRequest req;
      TryGetEntryResponse resp;
      req.set_key("my key");

      ctlr.Reset();
      impl.TryGetEntry(req, &resp, &ctlr);
      EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());

      ctlr.Reset();
      req.set_token("wrong token");
      impl.TryGetEntry(req, &resp, &ctlr);
      EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());

      for (auto&& e : {"token1", "token2"}) {
        ctlr.Reset();
        req.set_token(e);
        impl.TryGetEntry(req, &resp, &ctlr);
        EXPECT_EQ(STATUS_NOT_FOUND, ctlr.ErrorCode());  // RPC performed.
      }
    }
    {
      PutEntryRequest req;
      PutEntryResponse resp;
      ctlr.Reset();
      impl.PutEntry(req, &resp, &ctlr);
      EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());
    }

    {
      TryGetEntryRequest req;
      TryGetEntryResponse resp;
      req.set_key("my key");
      ctlr.Reset();
      impl.TryGetEntry(req, &resp, &ctlr);
      EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());
    }
  });
}

TEST(CacheServiceImpl, Ops) {
  flare::fiber::ExecutionContext::Create()->Execute([&] {
    CacheServiceImpl impl;

    {
      TryGetEntryRequest req;
      req.set_key("my key");
      req.set_token("token1");
      flare::RpcServerController ctlr;
      TryGetEntryResponse resp;
      impl.TryGetEntry(req, &resp, &ctlr);

      EXPECT_EQ(STATUS_NOT_FOUND, ctlr.ErrorCode());
    }

    {
      PutEntryRequest req;
      req.set_key("my key");
      req.set_token("token1");
      flare::RpcServerController ctlr;
      flare::testing::SetRpcServerRequestAttachment(
          &ctlr, flare::CreateBufferSlow("body"));
      PutEntryResponse resp;
      impl.PutEntry(req, &resp, &ctlr);

      ASSERT_FALSE(ctlr.Failed());
    }

    {
      TryGetEntryRequest req;
      req.set_key("my key");
      req.set_token("token1");
      flare::RpcServerController ctlr;
      TryGetEntryResponse resp;
      impl.TryGetEntry(req, &resp, &ctlr);

      EXPECT_EQ("body", flare::FlattenSlow(ctlr.GetResponseAttachment()));
    }
  });
}

}  // namespace yadcc::cache

FLARE_TEST_MAIN
