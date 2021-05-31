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

#include "yadcc/scheduler/scheduler_service_impl.h"

#include <chrono>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/fiber/execution_context.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/rpc_server_controller.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_controller.h"

using namespace std::literals;

FLARE_OVERRIDE_FLAG(acceptable_tokens, "token1,token2");
FLARE_OVERRIDE_FLAG(serving_daemon_token_rollout_interval, 1);

namespace yadcc::scheduler {

TEST(SchedulerServiceImpl, Token) {
  flare::fiber::ExecutionContext::Create()->Execute([&] {
    SchedulerServiceImpl impl;
    flare::RpcServerController ctlr;

    GetConfigRequest req;
    GetConfigResponse resp;

    impl.GetConfig(req, &resp, &ctlr);
    EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());

    ctlr.Reset();
    req.set_token("token1");
    impl.GetConfig(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());
    auto first_call = resp.serving_daemon_token();

    ctlr.Reset();
    req.set_token("token1");
    impl.GetConfig(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());
    auto second_call = resp.serving_daemon_token();

    EXPECT_EQ(first_call, second_call);

    // Wait for new token to be rolled out.
    std::this_thread::sleep_for(2s);

    ctlr.Reset();
    req.set_token("token1");
    impl.GetConfig(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());
    auto third_call = resp.serving_daemon_token();

    EXPECT_NE(first_call, third_call);
  });
}

}  // namespace yadcc::scheduler

FLARE_TEST_MAIN
