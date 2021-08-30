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

#include <optional>

#include "gtest/gtest.h"

#include "yadcc/common/parse_size.h"

namespace yadcc::cache {

TEST(ParseSize, All) {
  ASSERT_EQ(123, TryParseSize("123"));
  ASSERT_EQ(2048, TryParseSize("2K"));
  ASSERT_EQ(3145728, TryParseSize("3M"));
  ASSERT_EQ(1073741824, TryParseSize("1G"));
  ASSERT_EQ(std::nullopt, TryParseSize("3A"));
}

}  // namespace yadcc::cache
