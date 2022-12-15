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

#include <optional>
#include <string>
#include <vector>

#include "flare/base/dependency_registry.h"

DEFINE_string(
    disk_engine_cache_dirs, "10G,./cache",
    "A list of 'size,path' that specify where should we store cache data. If "
    "more than one directories are available, separated them by colon.");

DEFINE_string(disk_engine_action_on_misplaced_cache_entry, "delete",
              "Option to instruct how to react when hash mismatch over dirs. "
              "The one among of 'delete', 'move', 'ignore' is valid");

namespace yadcc::cache {

DiskCacheEngine::DiskCacheEngine()
    : disk_cache_impl_(DiskCache::Options{
          .shards = ParseCacheDirs(FLAGS_disk_engine_cache_dirs),
          .action_on_misplaced_cache_entry = ParseActionOnMisplacedEntry(
              FLAGS_disk_engine_action_on_misplaced_cache_entry)}) {}

std::vector<std::string> DiskCacheEngine::GetKeys() const {
  std::vector<std::string> result;
  return disk_cache_impl_.GetKeys();
}

std::optional<flare::NoncontiguousBuffer> DiskCacheEngine::TryGet(
    const std::string& key) const {
  return disk_cache_impl_.TryGet(key);
}

// Well, writing to (same physical) disk simultaneously from multiple thread
// can be slow. Things would be better if we perform writes in a thread
// dedicated to the destination disk.
//
// For the moment I don't bother doing that, but perhaps we should do it
// sometimes later.
void DiskCacheEngine::Put(const std::string& key,
                          const flare::NoncontiguousBuffer& bytes) {
  disk_cache_impl_.Put(key, bytes);
}

void DiskCacheEngine::Purge() { disk_cache_impl_.Purge(); }

Json::Value DiskCacheEngine::DumpInternals() const {
  Json::Value jsv;
  return disk_cache_impl_.DumpInternals();
}

FLARE_REGISTER_CLASS_DEPENDENCY(cache_engine_registry, "disk", DiskCacheEngine);

}  // namespace yadcc::cache
