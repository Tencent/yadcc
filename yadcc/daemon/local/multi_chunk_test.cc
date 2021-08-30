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

#include "yadcc/daemon/local/multi_chunk.h"

#include "gtest/gtest.h"

namespace yadcc::daemon::local {

TEST(MultiChunk, All) {
  auto result = MakeMultiChunk(
      {flare::CreateBufferSlow("something"), flare::CreateBufferSlow("fancy")});
  EXPECT_EQ("9,5\r\nsomethingfancy", flare::FlattenSlow(result));
  auto parsed = TryParseMultiChunk(result);
  ASSERT_TRUE(parsed);
  ASSERT_EQ(2, parsed->size());
  EXPECT_EQ("something", flare::FlattenSlow(parsed->at(0)));
  EXPECT_EQ("fancy", flare::FlattenSlow(parsed->at(1)));
}

TEST(MultiChunk, Invalid) {
  EXPECT_FALSE(
      TryParseMultiChunk(flare::CreateBufferSlow("9,5\r\nsomethingfanc")));
  EXPECT_FALSE(
      TryParseMultiChunk(flare::CreateBufferSlow("9,5\r\nsomethingfancy1")));
  EXPECT_FALSE(TryParseMultiChunk(flare::CreateBufferSlow("9\r\nsomethin")));
  EXPECT_FALSE(TryParseMultiChunk(flare::CreateBufferSlow("0\r\nasdf")));
}

TEST(MultiChunk, Empty) {
  EXPECT_TRUE(MakeMultiChunk({}).Empty());
  auto parsed = TryParseMultiChunk({});
  ASSERT_TRUE(parsed);
  ASSERT_TRUE(parsed->empty());
}

}  // namespace yadcc::daemon::local
