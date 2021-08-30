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
#include <set>

#include "gtest/gtest.h"

#include "flare/fiber/execution_context.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/rpc_server_controller.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_controller.h"

#include "yadcc/scheduler/task_dispatcher.h"

using namespace std::literals;

DECLARE_string(acceptable_user_tokens);
DECLARE_string(acceptable_servant_tokens);

FLARE_OVERRIDE_FLAG(serving_daemon_token_rollout_interval, 1);

namespace yadcc::scheduler {

TEST(SchedulerServiceImpl, Token) {
  flare::fiber::ExecutionContext::Create()->Execute([&] {
    FLAGS_acceptable_user_tokens = "token1,token2";
    FLAGS_acceptable_servant_tokens = "token1,token2";

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

TEST(SchedulerServiceImpl, TokenWithIntersection) {
  flare::fiber::ExecutionContext::Create()->Execute([&] {
    FLAGS_acceptable_user_tokens = "token1,token2";
    FLAGS_acceptable_servant_tokens = "token2,token3";
    TaskDispatcher::Instance()->servants_.servants.clear();

    SchedulerServiceImpl impl;
    flare::RpcServerController ctlr;

    HeartbeatRequest req;
    HeartbeatResponse resp;
    req.set_servant_priority(SERVANT_PRIORITY_USER);
    flare::testing::SetRpcServerRemotePeer(
        &ctlr, flare::EndpointFromString("192.0.2.1:12345"));

    ctlr.Reset();
    req.set_token("token1");
    req.set_location("192.0.2.128:6666");
    impl.Heartbeat(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());

    ctlr.Reset();
    req.set_token("token2");
    req.set_location("192.0.2.128:7777");
    impl.Heartbeat(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());

    ctlr.Reset();
    req.set_token("token3");
    req.set_location("192.0.2.128:8888");
    impl.Heartbeat(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());

    ctlr.Reset();
    req.set_token("token4");
    req.set_location("192.0.2.128:9999");
    impl.Heartbeat(req, &resp, &ctlr);
    EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());

    std::set<std::string> endpoints;
    for (auto&& e : TaskDispatcher::Instance()->servants_.servants) {
      endpoints.insert(e->personality.reported_location);
    }
    ASSERT_EQ(1, endpoints.count("192.0.2.128:6666"));
    ASSERT_EQ(1, endpoints.count("192.0.2.128:7777"));
    ASSERT_EQ(1, endpoints.count("192.0.2.128:8888"));
    ASSERT_EQ(0, endpoints.count("192.0.2.128:9999"));
  });
}

TEST(SchedulerServiceImpl, TokenWithoutIntersection) {
  flare::fiber::ExecutionContext::Create()->Execute([&] {
    FLAGS_acceptable_user_tokens = "token1";
    FLAGS_acceptable_servant_tokens = "token2";
    TaskDispatcher::Instance()->servants_.servants.clear();

    SchedulerServiceImpl impl;
    flare::RpcServerController ctlr;

    HeartbeatRequest req;
    HeartbeatResponse resp;
    req.set_servant_priority(SERVANT_PRIORITY_USER);
    flare::testing::SetRpcServerRemotePeer(
        &ctlr, flare::EndpointFromString("192.0.2.1:12345"));

    ctlr.Reset();
    req.set_token("token1");
    req.set_location("192.0.2.128:6666");
    impl.Heartbeat(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());

    ctlr.Reset();
    req.set_token("token2");
    req.set_location("192.0.2.128:7777");
    impl.Heartbeat(req, &resp, &ctlr);
    ASSERT_FALSE(ctlr.Failed());

    ctlr.Reset();
    req.set_token("token3");
    req.set_location("192.0.2.128:8888");
    impl.Heartbeat(req, &resp, &ctlr);
    EXPECT_EQ(STATUS_ACCESS_DENIED, ctlr.ErrorCode());

    std::set<std::string> endpoints;
    for (auto&& e : TaskDispatcher::Instance()->servants_.servants) {
      endpoints.insert(e->personality.reported_location);
      FLARE_LOG_ERROR("{}", e->personality.reported_location);
    }
    ASSERT_EQ(1, endpoints.count("192.0.2.128:6666"));
    ASSERT_EQ(1, endpoints.count("192.0.2.128:7777"));
    ASSERT_EQ(0, endpoints.count("192.0.2.128:8888"));
  });
}

}  // namespace yadcc::scheduler

FLARE_TEST_MAIN
