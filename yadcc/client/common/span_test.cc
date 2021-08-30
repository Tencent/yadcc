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

#include "yadcc/client/span.h"

#include "gtest/gtest.h"

namespace yadcc::client {

TEST(Span, All) {
  Span<int> span;
  EXPECT_TRUE(span.empty());

  int values[] = {1, 2, 3};
  Span<int> span2(values, 3);
  EXPECT_EQ(3, span2.size());
  EXPECT_EQ(3, span2.back());

  Span<int> span3(values, values + 3);
  EXPECT_EQ(3, span3.size());
  EXPECT_EQ(3, span3.back());
}

}  // namespace yadcc::client
