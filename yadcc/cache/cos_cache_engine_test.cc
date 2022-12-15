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

#include "yadcc/cache/cos_cache_engine.h"

#include <chrono>

#include "gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/random.h"
#include "flare/base/string.h"
#include "flare/init/override_flag.h"
#include "flare/net/cos/cos_status.h"
#include "flare/net/cos/ops/bucket/get_bucket.h"
#include "flare/net/cos/ops/object/get_object.h"
#include "flare/net/cos/ops/object/put_object.h"
#include "flare/testing/cos_mock.h"
#include "flare/testing/main.h"

FLARE_OVERRIDE_FLAG(cos_engine_cos_uri, "mock://whatever-it-is");

namespace yadcc::cache {

namespace {

// Using `std::unordered_map<...>` (without wrapped by `flare::NeverDestroyed`)
// leads to (a one-time) leak.
flare::NeverDestroyed<
    std::unordered_map<std::string, flare::NoncontiguousBuffer>>
    objects;

flare::Status HandleGetBucket(const flare::CosGetBucketRequest& req,
                              flare::CosGetBucketResult* result) {
  for (auto&& [k, v] : *objects) {
    if (flare::StartsWith(k, req.prefix)) {
      auto&& added = result->contents.emplace_back();
      added.key = k;
      added.size = v.ByteSize();
      added.last_modified = "2020-12-31T00:00:00.000Z";
    }
  }
  return flare::Status();
}

flare::Status HandleGetObject(const flare::CosGetObjectRequest& req,
                              flare::CosGetObjectResult* result) {
  if (auto iter = objects->find(req.key); iter != objects->end()) {
    result->bytes = iter->second;
    return flare::Status();
  } else {
    return flare::Status(flare::CosStatus::NoSuchKey);
  }
}

flare::Status HandlePutObject(const flare::CosPutObjectRequest& req,
                              flare::CosPutObjectResult* result) {
  (*objects)[req.key] = req.bytes;
  return flare::Status();
}

}  // namespace

TEST(CosCacheEngine, All) {
  CosCacheEngine engine;

  FLARE_EXPECT_COS_OP(GetBucket).WillRepeatedly(
      flare::testing::HandleCosOp(HandleGetBucket));
  FLARE_EXPECT_COS_OP(GetObject).WillRepeatedly(
      flare::testing::HandleCosOp(HandleGetObject));
  FLARE_EXPECT_COS_OP(PutObject).WillRepeatedly(
      flare::testing::HandleCosOp(HandlePutObject));

  EXPECT_TRUE(engine.GetKeys().empty());
  EXPECT_FALSE(engine.TryGet("my key"));
  engine.Put("my key", flare::CreateBufferSlow("my value"));

  auto keys = engine.GetKeys();
  ASSERT_EQ(1, keys.size());
  EXPECT_EQ("my key", keys[0]);
  auto value = engine.TryGet("my key");
  ASSERT_TRUE(value);
  EXPECT_EQ("my value", flare::FlattenSlow(*value));
}

}  // namespace yadcc::cache

FLARE_TEST_MAIN
