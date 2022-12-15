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

#include "yadcc/client/common/utility.h"

#include "gtest/gtest.h"

namespace yadcc::client {

TEST(Split, All) {
  auto split = Split("/a/b/c/d/e/f///g", "/");
  ASSERT_EQ(7, split.size());
  ASSERT_EQ("a", split[0]);
  ASSERT_EQ("b", split[1]);
  ASSERT_EQ("c", split[2]);
  ASSERT_EQ("d", split[3]);
  ASSERT_EQ("e", split[4]);
  ASSERT_EQ("f", split[5]);
  ASSERT_EQ("g", split[6]);
}

TEST(Join, All) {
  EXPECT_EQ("a,b,c,d,e", Join({"a", "b", "c", "d", "e"}, ","));
}

}  // namespace yadcc::client
