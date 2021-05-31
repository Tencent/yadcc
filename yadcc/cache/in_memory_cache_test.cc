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

#include "yadcc/cache/in_memory_cache.h"

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/random.h"
#include "flare/base/string.h"

namespace yadcc::cache {

TEST(InMemoryCache, All) {
  InMemoryCache in_memory_cache(10000);

  // Fill t1 cache.
  for (int i = 0; i != 100; ++i) {
    in_memory_cache.Put(
        flare::Format("my-key-{}", i),
        flare::CreateBufferSlow("my value" + std::string(92, i)));
  }

  // Entries from t1 transfer to t2.
  for (int i = 0; i != 100; ++i) {
    auto result = in_memory_cache.TryGet(flare::Format("my-key-{}", i));
    EXPECT_TRUE(result);
  }

  // Fill t1 cache use more new entries. Since the intial state is more like
  // LFU, the result is that key 199 is in t1 and key 100-198 are in b1.
  for (int i = 0; i != 100; ++i) {
    auto result = in_memory_cache.TryGet(flare::Format("my-key-{}", i + 100));
    EXPECT_TRUE(!result);
    in_memory_cache.Put(
        flare::Format("my-key-{}", i + 100),
        flare::CreateBufferSlow("my value" + std::string(92, i + 100)));
  }

  // Check And move key 100-199 back to t2 from b1. The mode is move to LRU
  // gradually
  for (int i = 0; i != 99; ++i) {
    auto result = in_memory_cache.TryGet(flare::Format("my-key-{}", i + 100));

    EXPECT_TRUE(!result);
    in_memory_cache.Put(
        flare::Format("my-key-{}", i + 100),
        flare::CreateBufferSlow("my value" + std::string(92, i + 100)));
  }

  auto result = in_memory_cache.TryGet(flare::Format("my-key-{}", 99 + 100));
  EXPECT_TRUE(result);

  // Check And move key 0-99 from b2 to t2. The mode is move to LFU gradually
  for (int i = 0; i != 100; ++i) {
    auto result = in_memory_cache.TryGet(flare::Format("my-key-{}", i));
    EXPECT_TRUE(!result);
    in_memory_cache.Put(
        flare::Format("my-key-{}", i),
        flare::CreateBufferSlow("my value" + std::string(92, i)));
  }

  // Check cache key 0-99.
  for (int i = 0; i != 100; ++i) {
    auto result = in_memory_cache.TryGet(flare::Format("my-key-{}", i));
    EXPECT_TRUE(result);
  }

  // Remove key 0.
  in_memory_cache.Remove({"my-key-0"});
  EXPECT_TRUE(!in_memory_cache.TryGet("my-key-0"));

  // Overwrite key 1.
  EXPECT_TRUE(!!in_memory_cache.TryGet("my-key-1"));

  std::string overwrite_value = "overwrite value";
  in_memory_cache.Put("my-key-1", flare::CreateBufferSlow(overwrite_value));
  auto overwrite_result = in_memory_cache.TryGet("my-key-1");
  EXPECT_TRUE(!!overwrite_result);
  EXPECT_TRUE(flare::FlattenSlow(*overwrite_result) == overwrite_value);
}

}  // namespace yadcc::cache
