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

#include "yadcc/cache/disk_cache_engine.h"

#include <chrono>
#include <fstream>

#include "gflags/gflags_declare.h"
#include "gtest/gtest.h"

#include "flare/base/buffer.h"
#include "flare/base/random.h"
#include "flare/base/string.h"
#include "flare/init/override_flag.h"

#include "yadcc/common/dir.h"

using namespace std::literals;

DECLARE_string(disk_engine_cache_dirs);
DECLARE_string(disk_engine_action_on_misplaced_cache_entry);

namespace yadcc::cache {

TEST(DiskCacheEngine, MutilDirs) {
  {
    FLAGS_disk_engine_cache_dirs =
        "1048576,./multicache/0:1048576,./multicache/1";
    DiskCacheEngine cache;
    for (int i = 0; i != 100; ++i) {
      cache.Put(flare::Format("my-key-{}", i),
                flare::CreateBufferSlow("my value" + std::string(i, '0')));
    }

    for (int i = 0; i != 100; ++i) {
      EXPECT_TRUE(!!cache.TryGet(flare::Format("my-key-{}", i)));
    }
  }

  {
    FLAGS_disk_engine_cache_dirs =
        "1048576,./multicache/0:1048576,./multicache/1:1048576,./multicache/2";
    FLAGS_disk_engine_action_on_misplaced_cache_entry = "move";
    DiskCacheEngine cache;

    for (int i = 0; i != 100; ++i) {
      EXPECT_TRUE(!!cache.TryGet(flare::Format("my-key-{}", i)));
    }
  }
}

TEST(DiskCacheEngine, All) {
  FLAGS_disk_engine_cache_dirs = "1048576,./cache";
  DiskCacheEngine cache;

  // Here we inserted more than `kMaxBytes` into the cache.
  for (int i = 0; i != 10000; ++i) {
    cache.Put(flare::Format("my-key-{}", i),
              flare::CreateBufferSlow("my value" + std::string(i, '0')));
  }

  // All the keys should be there.
  for (int i = 0; i != 12345; ++i) {
    auto key = flare::Random(1, 10000 - 1);
    auto expected = "my value" + std::string(key, '0');
    auto optv = cache.TryGet(flare::Format("my-key-{}", key));
    ASSERT_TRUE(optv);
    EXPECT_EQ(expected, flare::FlattenSlow(*optv));
  }

  // Keep [1, 100] hot, so that they won't be discarded by `Purge`.
  std::this_thread::sleep_for(2s);
  for (int i = 0; i != 100; ++i) {
    cache.TryGet(flare::Format("my-key-{}", i));
  }

  // Discard some entries to keep size under limit.
  cache.Purge();

  // They shouldn't be discarded.
  for (int i = 0; i != 100; ++i) {
    EXPECT_EQ("my value" + std::string(i, '0'),
              flare::FlattenSlow(*cache.TryGet(flare::Format("my-key-{}", i))));
  }
  // And the size should drop by now.
  auto now_used_bytes = 0;
  auto keys_discarded = 0;
  for (int i = 0; i != 10000; ++i) {
    auto optv = cache.TryGet(flare::Format("my-key-{}", i));
    if (optv) {
      now_used_bytes += optv->ByteSize();
    } else {
      ++keys_discarded;
    }
  }

  // Overwrite should work.
  cache.Put("my-key-1", flare::CreateBufferSlow("my new value"));
  EXPECT_EQ("my new value", flare::FlattenSlow(*cache.TryGet("my-key-1")));

  EXPECT_LE(now_used_bytes, 1048576);
  EXPECT_GT(keys_discarded, 0);

  static constexpr auto kHealthyEntries = 10;
  auto not_touched = 0;

  // Now make the cache data corrupt and see if we could realize the
  // corruption.
  auto files = EnumerateDir("./cache");
  for (auto&& dir1 : files) {
    auto subdir1 = EnumerateDir(flare::Format("./cache/{}", dir1.name));
    for (auto&& dir2 : subdir1) {
      auto subdir2 =
          EnumerateDir(flare::Format("./cache/{}/{}", dir1.name, dir2.name));
      for (auto&& e : subdir2) {
        auto file =
            flare::Format("./cache/{}/{}/{}", dir1.name, dir2.name, e.name);

        if (not_touched < kHealthyEntries) {
          ++not_touched;
          continue;
        }
        if (flare::Random() % 2 == 0) {
          // Overwrite it.
          std::fstream fs(file, std::fstream::trunc | std::fstream::out);
          auto size = flare::Random(1, 1234);
          for (int i = 0; i != size; ++i) {
            fs << flare::Random<char>();
          }
          FLARE_CHECK(fs);
        } else {
          // Append to it.
          std::fstream fs(file, std::fstream::app | std::fstream::binary |
                                    std::fstream::out);
          auto size = flare::Random(1, 1234);
          for (int i = 0; i != size; ++i) {
            fs << flare::Random<char>();
          }
          FLARE_CHECK(fs);
        }
      }
    }
  }

  auto healthy = 0;
  for (int i = 0; i != 10000; ++i) {
    healthy += !!cache.TryGet(flare::Format("my-key-{}", i));
  }
  EXPECT_EQ(kHealthyEntries, healthy);
}

}  // namespace yadcc::cache
