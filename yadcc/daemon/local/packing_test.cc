// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "yadcc/daemon/local/packing.h"

#include "gtest/gtest.h"
#include "jsoncpp/json.h"

#include "flare/base/buffer.h"
#include "flare/testing/message.pb.h"

#include "yadcc/daemon/local/multi_chunk.h"

namespace yadcc::daemon::local {

TEST(Packing, ParseJsonAsMessage) {
  auto parsed =
      TryParseJsonAsMessage<flare::testing::One>(R"({"str":"a","integer":5})");
  ASSERT_TRUE(parsed);
  EXPECT_EQ("a", parsed->str());
  EXPECT_EQ(5, parsed->integer());
  EXPECT_FALSE(TryParseJsonAsMessage<flare::testing::One>(
      R"({"str":"a","integer":"a"})"));
}

TEST(Packing, ParseMultiChunkAsMessage) {
  auto multi_chunk = MakeMultiChunk(
      {flare::CreateBufferSlow(R"({"str":"a","integer":5})"),
       flare::CreateBufferSlow("buffer1"), flare::CreateBufferSlow("buffer2")});
  auto parsed = TryParseMultiChunkRequest<flare::testing::One>(multi_chunk);
  ASSERT_TRUE(parsed);
  auto&& [req, bytes] = *parsed;
  ASSERT_EQ(2, bytes.size());
  EXPECT_EQ("a", req.str());
  EXPECT_EQ(5, req.integer());
  EXPECT_EQ("buffer1", flare::FlattenSlow(bytes[0]));
  EXPECT_EQ("buffer2", flare::FlattenSlow(bytes[1]));
}

TEST(Packing, WriteMessageAsJson) {
  flare::testing::One one;
  one.set_str("1");
  one.set_integer(1);
  auto str = WriteMessageAsJson(one);

  Json::Value jsv;
  ASSERT_TRUE(Json::Reader().parse(str, jsv));
  EXPECT_EQ("1", jsv["str"].asString());
  EXPECT_EQ(1, jsv["integer"].asUInt());
}

TEST(Packing, WriteMultiChunkResponse) {
  flare::testing::One one;
  one.set_str("1");
  one.set_integer(1);
  auto packed = WriteMultiChunkResponse(
      one,
      {flare::CreateBufferSlow("buffer1"), flare::CreateBufferSlow("buffer2")});

  auto parsed = TryParseMultiChunkRequest<flare::testing::One>(packed);
  ASSERT_TRUE(parsed);
  auto&& [req, bytes] = *parsed;
  ASSERT_EQ(2, bytes.size());
  EXPECT_EQ("1", req.str());
  EXPECT_EQ(1, req.integer());
  EXPECT_EQ("buffer1", flare::FlattenSlow(bytes[0]));
  EXPECT_EQ("buffer2", flare::FlattenSlow(bytes[1]));
}

}  // namespace yadcc::daemon::local
