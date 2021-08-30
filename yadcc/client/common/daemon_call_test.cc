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

#include "yadcc/client/common/daemon_call.h"

#include <chrono>
#include <string>

#include "gtest/gtest.h"

#include "flare/base/net/endpoint.h"
#include "flare/base/random.h"
#include "flare/fiber/this_fiber.h"
#include "flare/rpc/http_handler.h"
#include "flare/rpc/server.h"
#include "flare/testing/endpoint.h"
#include "flare/testing/main.h"

using namespace std::literals;

namespace yadcc::client {

namespace {

std::string RandomString() {
  std::string result(flare::Random(128), 0);
  for (auto&& e : result) {
    e = flare::Random(255);
  }
  return result;
}

}  // namespace

TEST(DaemonCall, All) {
  auto listening_ep = flare::testing::PickAvailableEndpoint();
  setenv("YADCC_DAEMON_PORT",
         std::to_string(EndpointGetPort(listening_ep)).c_str(), 1);
  flare::Server server{
      flare::Server::Options{.maximum_packet_size = 128 * 1048576}};

  server.AddHttpHandler(
      "/fancy/api",
      flare::NewHttpPostHandler([](auto&& req, auto&& resp, auto&& ctx) {
        EXPECT_EQ("/fancy/api", req.uri());
        EXPECT_EQ("abc", req.headers()->TryGet("X-My-Header"));
        EXPECT_EQ("body", *req.body());
        resp->set_body("fancy response");
      }));
  server.AddHttpHandler(
      "/fancy/echo",
      flare::NewHttpPostHandler([](auto&& req, auto&& resp, auto&& ctx) {
        resp->set_body(*req.body());
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
  {
    auto body1 = std::string(12345678, 'a');
    auto body2 = std::string(23456789, 'b');
    auto body3 = std::string(1, 'c');
    auto body4 = std::string(2452135, 'd');
    auto body5 = std::string(888, 'e');
    auto&& [status, body] =
        DaemonCallGathered("/fancy/echo", {"X-My-Header: abc"},
                           {body1, body2, body3, body4, body5}, 20s);
    EXPECT_EQ(200, status);
    EXPECT_EQ(body1 + body2 + body3 + body4 + body5, body);
  }
  for (int k = 0; k != 1234; ++k) {
    std::vector<std::string> bodies;
    std::vector<std::string_view> body_views;
    for (int i = 0; i != k * 13; ++i) {
      bodies.push_back(RandomString());
    }
    for (int i = 0; i != k * 13; ++i) {
      body_views.push_back(bodies[i]);
    }

    auto&& [status, body] = DaemonCallGathered(
        "/fancy/echo", {"X-My-Header: abc"}, {body_views}, 20s);
    EXPECT_EQ(200, status);
    EXPECT_EQ(flare::Join(body_views, ""), body);
  }
  {
    auto&& [status, body] =
        DaemonCall("/fancy/echo", {"X-My-Header: abc"}, "", 1s);
    EXPECT_EQ(200, status);
    EXPECT_EQ("", body);
  }
  EXPECT_LT(
      DaemonCall("/fancy/timeout", {"X-My-Header: abc"}, "body", 1s).status, 0);
}

TEST(DaemonCall, Mock) {
  SetDaemonCallGatheredHandler(
      [](auto&&...) { return DaemonResponse{.status = -1}; });
  EXPECT_EQ(-1, DaemonCallGathered("", {}, {}, 1s).status);
  SetDaemonCallGatheredHandler(nullptr);
}

}  // namespace yadcc::client

FLARE_TEST_MAIN
