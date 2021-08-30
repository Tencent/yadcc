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

#ifndef YADCC_COMMON_DISK_CACHE_H_
#define YADCC_COMMON_DISK_CACHE_H_

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "flare/base/buffer.h"
#include "flare/base/handle.h"

#include "yadcc/common/consistent_hash.h"

namespace yadcc {

// A simple, file-based, on-disk cache.
//
// Thread-safe.
class DiskCache {
 public:
  enum class ActionOnMisplacedEntry { Delete, Move, Ignore };

  struct Options {
    // We allow the user to save cache to multiple shards (usually in different
    // physical drives.).
    std::vector<std::pair<std::string, std::size_t>> shards;

    // Action to take if a misplaced (in terms of sharding algorithm) cache
    // entry is found.
    ActionOnMisplacedEntry action_on_misplaced_cache_entry =
        ActionOnMisplacedEntry::Delete;

    // To avoid store too many files in each shard, internall we create several
    // level of sub-directories to make them hierarchical.
    std::size_t sub_dir_level = 2;
    std::size_t sub_dirs = 16;
  };

 public:
  // You can specify the options to take an effect on running of disk cache. A
  // way of customization.
  explicit DiskCache(Options options);

  // Enumerate keys of cache entries.
  std::vector<std::string> GetKeys() const;

  // Get value of the given key, if it exists.
  std::optional<flare::NoncontiguousBuffer> TryGet(
      const std::string& key) const;

  // Add a new cache entry or replace an existing one (rare).
  void Put(const std::string& key, const flare::NoncontiguousBuffer& bytes);

  // If we've had too many cache entries, this method discard some old entries
  // to make space.
  //
  // It's slow, and may block `TryGet` / `Put`, so don't call it too often.
  void Purge();

  // Dumps internals about the cache.
  Json::Value DumpInternals() const;

 private:
  void InitializeWorkspaceAt(const std::string& path);

  // To transform to the weight of directories according to the each size.
  std::map<std::string, std::uint64_t> GetWeightedDirs(
      const std::vector<std::pair<std::string, std::uint64_t>>& directories);

  // Determine which file contains value of the given key.
  std::optional<std::string> TryGetPathOfKey(const std::string& key,
                                             bool record = false) const;

  // Purge a cache directory.
  std::vector<std::string> PurgeCacheAt(const std::string& path,
                                        std::uint64_t size_limit);

  // Return value describes each file key in the specified dir. We also want the
  // file size of each key, so the second field of pair describe it.
  std::unordered_map<std::string,
                     std::vector<std::pair<std::string, std::size_t>>>
  GetEntryKeysSnapshot() const;

  // Help us figure out how much keys and how many byte-size used per dir.
  std::unordered_map<std::string, std::pair<std::size_t, std::size_t>>
  GetKeyAndByteSizePerDir() const;

  // Create a new entry. Returns: [handle, dir_lock, cache_entry_lock].
  std::tuple<flare::Handle, std::unique_lock<std::shared_mutex>,
             std::unique_lock<std::shared_mutex>>
  CreateEntryLocked(const std::string& key, const std::string& path);

 private:
  struct EntryDesc {
    std::shared_mutex entry_lock;
    std::size_t file_size;
    std::atomic<std::chrono::nanoseconds> last_accessed;
  };

  struct EntriesInDir {
    std::shared_mutex dir_lock;

    // Make sure you hold shared ownership on `dir_lock` during access to
    // entries. Holding `EntryDesc.entry_lock` only is NOT safe.
    std::unordered_map<std::string, std::unique_ptr<EntryDesc>> entries;
  };

  // Introduce some customization options.
  Options options_;

  // Maps user's key to `options_.shards`.
  ConsistentHash shard_mapper_;

  // The structure (keys) of this map is initialized on start up, only its
  // values are touched, therefore no locking is required here.
  std::unordered_map<std::string, std::unique_ptr<EntriesInDir>>
      entries_per_dir_;

  // We never insert new keys into `shard_hits_`, only its value is mutated.
  // Therefore no locking is required.
  std::unordered_map<std::string, std::unique_ptr<std::atomic<std::size_t>>>
      shard_hits_;
  mutable std::atomic<std::size_t> cache_fills_{}, cache_hits_{},
      cache_misses_{};
  mutable std::atomic<std::size_t> cache_overwrites_{};
};

// An Utility to help parse a directories config.
std::vector<std::pair<std::string, std::uint64_t>> ParseCacheDirs(
    const std::string& dirs);

DiskCache::ActionOnMisplacedEntry ParseActionOnMisplacedEntry(
    const std::string& config);

}  // namespace yadcc

#endif  // YADCC_COMMON_DISK_CACHE_H_
