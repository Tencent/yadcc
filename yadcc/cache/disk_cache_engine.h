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

#ifndef YADCC_CACHE_DISK_CACHE_ENGINE_H_
#define YADCC_CACHE_DISK_CACHE_ENGINE_H_

#include <optional>
#include <string>
#include <vector>

#include "flare/base/buffer.h"

#include "yadcc/cache/cache_engine.h"
#include "yadcc/common/disk_cache.h"

namespace yadcc::cache {

// A simple, file-based, on-disk cache.
//
// Thread-safe.
class DiskCacheEngine : public CacheEngine {
 public:
  // We support supplying multiple directories here. Cache entries are sharded
  // between them.
  //
  // Directories are supplied as a list of `(path, max_size)`.
  //
  // Internally we use a consistent hash to shard cache entries, so you can
  // safely add or remove directories between different run, without hurting
  // cache hit ratio too much.).
  DiskCacheEngine();

  // Enumerate keys of cache entries.
  std::vector<std::string> GetKeys() const override;

  // Get value of the given key, if it exists.
  std::optional<flare::NoncontiguousBuffer> TryGet(
      const std::string& key) const override;

  // Add a new cache entry or replace an existing one (rare).
  void Put(const std::string& key,
           const flare::NoncontiguousBuffer& bytes) override;

  // If we've had too many cache entries, this method discard some old entries
  // to make space.
  //
  // It's slow, and may block `TryGet` / `Put`, so don't call it too often.
  void Purge() override;

  // Dumps internals about the cache.
  Json::Value DumpInternals() const override;

 private:
  DiskCache disk_cache_impl_;
};

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_DISK_CACHE_ENGINE_H_
