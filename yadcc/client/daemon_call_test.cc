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

#include "yadcc/client/daemon_call.h"

#include <chrono>
#include <string>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/net/endpoint.h"
#include "flare/fiber/this_fiber.h"
#include "flare/rpc/http_handler.h"
#include "flare/rpc/server.h"
#include "flare/testing/endpoint.h"
#include "flare/testing/main.h"

using namespace std::literals;

namespace yadcc::client {

TEST(DaemonCall, All) {
  auto listening_ep = flare::testing::PickAvailableEndpoint();
  setenv("YADCC_DAEMON_PORT",
         std::to_string(EndpointGetPort(listening_ep)).c_str(), 1);
  flare::Server server;

  server.AddHttpHandler(
      "/fancy/api",
      flare::NewHttpPostHandler([](auto&& req, auto&& resp, auto&& ctx) {
        EXPECT_EQ("/fancy/api", req.uri());
        EXPECT_EQ("abc", req.headers()->TryGet("X-My-Header"));
        EXPECT_EQ("body", *req.body());
        resp->set_body("fancy response");
      }));
  server.AddHttpHandler("/fancy/timeout",
                        flare::NewHttpPostHandler([](auto&&...) {
                          flare::this_fiber::SleepFor(2s);
                        }));
  server.ListenOn(listening_ep);
  server.Start();
  {
    auto&& [status, body] =
        DaemonCall("/fancy/api", {"X-My-Header: abc"}, "body", 1s);
    EXPECT_EQ(200, status);
    EXPECT_EQ("fancy response", body);
  }
  EXPECT_LT(
      DaemonCall("/fancy/timeout", {"X-My-Header: abc"}, "body", 1s).status, 0);
}

}  // namespace yadcc::client

FLARE_TEST_MAIN
