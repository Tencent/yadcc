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

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "thirdparty/jsoncpp/json.h"

#include "flare/base/crypto/blake3.h"
#include "flare/base/deferred.h"
#include "flare/base/dependency_registry.h"
#include "flare/base/encoding.h"
#include "flare/base/handle.h"
#include "flare/base/string.h"

#include "yadcc/cache/dir.h"
#include "yadcc/common/io.h"
#include "yadcc/common/xxhash.h"

DEFINE_string(
    cache_dirs, "10G,./cache",
    "A list of 'size,path' that specify where should we store cache data. If "
    "more than one directories are available, separated them by colon.");

DEFINE_string(action_on_misplaced_cache_entry, "delete",
              "Option to instruct how to react when hash mismatch over dirs. "
              "The one among of 'delete', 'move', 'ignore' is valid");

namespace yadcc::cache {

namespace {

// We scatter files into several directories, to keep number of files in each of
// them managable.
constexpr auto kSubdirs = 16;
constexpr auto kSubdirLevel = 2;

// We don't take endian into consideration here, as we don't support migrating
// cache between different machines.
struct alignas(64) FileHeader {
  // For detecting disk corruption, partial write, etc.
  char checksum[32];

  // ... reserved for future use.
};

static_assert(sizeof(FileHeader) == 64);

std::vector<std::pair<std::string, std::uint64_t>> ParseCacheDirs() {
  auto splitted = flare::Split(FLAGS_cache_dirs, ":");
  std::vector<std::pair<std::string, std::uint64_t>> result;
  for (auto&& e : splitted) {
    auto kv = flare::Split(e, ",");
    FLARE_CHECK_EQ(kv.size(), 2, "Invalid directory: {}", e);
    auto size_str = kv[0];
    std::uint64_t scale = 1;
    if (size_str.back() == 'G') {
      scale = 1 << 30;
      size_str.remove_suffix(1);
    } else if (size_str.back() == 'M') {
      scale = 1 << 20;
      size_str.remove_suffix(1);
    } else if (size_str.back() == 'K') {
      scale = 1 << 10;
      size_str.remove_suffix(1);
    }
    auto size = flare::TryParse<std::uint64_t>(size_str);
    FLARE_CHECK(size, "Invalid size: {}", size_str);
    result.emplace_back(kv[1], *size * scale);
    FLARE_LOG_INFO(
        "Using directory [{}] to store cache entries. We'll be using up to {} "
        "bytes (soft limit) here.",
        result.back().first, result.back().second);
  }
  return result;
}

// Marshals cache key to a string that's safe to be used as file name.
//
// Security consideration: Unless `key` is trustworthy, blindly using `key` as
// (a part of) file name imposes a security risk. If an adversary constructs a
// file name containing `/..` or something alike, we may overwrite file outside
// of our workspace.
std::string MarshalKey(const std::string& key) {
  // Pct-encoded is safe to use in path.
  return flare::EncodePercent(key);
}

std::optional<std::string> GetKeyFromPath(const std::string& path) {
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return flare::DecodePercent(path.substr(pos + 1));
}

std::array<int, kSubdirLevel> GetSubdirsFor(std::size_t hash) {
  // TODO(luobogao): Make sure `kSubdirs ** kSubdirLevel < UINT64_MAX`.
  std::array<int, kSubdirLevel> result;
  for (int i = 0; i != kSubdirLevel; ++i) {
    result[i] = hash % kSubdirs;
    hash /= kSubdirs;
  }
  return result;
}

flare::NoncontiguousBuffer WriteFileHeader(
    const flare::NoncontiguousBuffer& buffer) {
  FileHeader header;
  memcpy(header.checksum, flare::Blake3(buffer).data(), 32);
  return flare::CreateBufferSlow(&header, sizeof(header));
}

std::optional<FileHeader> VerifyEntryAndCutHeader(
    flare::NoncontiguousBuffer* buffer) {
  if (buffer->ByteSize() < sizeof(FileHeader)) {
    FLARE_LOG_WARNING_EVERY_SECOND("No valid header was found.");
    return std::nullopt;
  }
  FileHeader header;
  flare::FlattenToSlow(*buffer, &header, sizeof(FileHeader));
  buffer->Cut(sizeof(FileHeader));
  if (std::string_view(header.checksum, 32) != flare::Blake3(*buffer)) {
    FLARE_LOG_WARNING_EVERY_SECOND("Checksum mismatch, on-disk corruption?");
    return std::nullopt;
  }
  return header;
}

std::vector<std::string> SanitizeWorkspaceAndEnumerate(const std::string& dir) {
  constexpr auto kLevelToFile = kSubdirLevel + 1;
  auto level = 0;
  std::vector<std::string> dirs{dir};

  // Iteratively enumerate entries in `dirs`.
  while (level++ != kLevelToFile) {
    std::vector<std::string> elements;
    for (auto& e : dirs) {
      auto subs = EnumerateDir(e);
      for (auto&& ee : subs) {
        auto full_name = flare::Format("{}/{}", e, ee.name);
        if (level == kLevelToFile && ee.is_dir) {
          // Someone else created the directory in our workspace?
          FLARE_LOG_WARNING(
              "Directory is found at [{}] unexpectedly, removing.", full_name);
          RemoveDirs(full_name);
        } else if (level != kLevelToFile && !ee.is_dir) {
          // Someone else created the file in our workspace?
          FLARE_LOG_WARNING(
              "Non-directory is found at [{}] unexpectedly, removing.",
              full_name);
          FLARE_PCHECK(unlink(full_name.c_str()) == 0);
        } else {
          elements.push_back(full_name);
        }
      }
    }
    dirs = elements;  // Keep going deep.
  }
  return dirs;  // It's file names by now.
}

struct FileInfo {
  std::string path;
  off_t size;
  std::chrono::system_clock::time_point last_used;  // `mtime`.
};

std::optional<FileInfo> GetFileInfo(const std::string& path) {
  struct stat st;
  FLARE_PCHECK(lstat(path.c_str(), &st) == 0);
  return FileInfo{
      .path = path,
      .size = st.st_size,
      .last_used = std::chrono::system_clock::from_time_t(st.st_mtime)};
}

std::vector<FileInfo> EnumerateCacheEntries(const std::string& path) {
  std::vector<FileInfo> result;
  for (auto&& e : SanitizeWorkspaceAndEnumerate(path)) {
    auto opt = GetFileInfo(e);
    FLARE_CHECK(opt, "Failed to access file [{}].", e);
    result.push_back(*opt);
  }
  return result;
}

std::string GetDirFromPath(const std::string& path) {
  auto pos = path.find_last_of('/');
  FLARE_PCHECK(pos != std::string::npos);
  return path.substr(0, pos);
}

}  // namespace

DiskCacheEngine::DiskCacheEngine()
    : dirs_(ParseCacheDirs()), shard_mapper_(GetWeightedDirs(dirs_), XxHash()) {
  // We must initialize cache dirs firstly to avoid some obvious problems.
  for (auto&& [path, _] : dirs_) {
    InitializeWorkspaceAt(path);
    // Insert every dir entry into map to avoid expecetions during using .at
    // function.
    shard_hits_[path] = std::make_unique<std::atomic<std::size_t>>(0);
  }

  // Load existing cache data.
  for (auto&& [path, _] : dirs_) {
    for (auto&& file : EnumerateCacheEntries(path)) {
      auto key = GetKeyFromPath(file.path);
      if (!key) {
        FLARE_LOG_WARNING("Found invalid cache file at [{}]", file.path);
        continue;  // Ignored.
      }

      auto dst_path = TryGetPathOfKey(*key);
      if (!dst_path) {
        FLARE_LOG_WARNING(
            "Found invalid cache file at [{}], key[{}]. We can't move it to "
            "destination path.",
            file.path, *key);
        continue;  // Ignored.
      }
      auto dst_dir = GetDirFromPath(*dst_path);
      auto dir = GetDirFromPath(file.path);
      if (dst_dir != dir) {
        if (FLAGS_action_on_misplaced_cache_entry == "move") {
          FLARE_PCHECK(std::rename(file.path.c_str(), dst_path->c_str()) == 0);
          dir.swap(dst_dir);
        } else if (FLAGS_action_on_misplaced_cache_entry == "delete") {
          FLARE_PCHECK(unlink(file.path.c_str()) == 0, "Failed to remove [{}].",
                       file.path);
          continue;
        } else if (FLAGS_action_on_misplaced_cache_entry == "ignore") {
          continue;
        } else {
          FLARE_LOG_FATAL(
              "Invalid option value [{}] of action_on_misplaced_cache_entry.",
              FLAGS_action_on_misplaced_cache_entry);
        }
      }
      auto&& entry = entries_per_dir_.at(dir);
      std::scoped_lock _(entry->dir_lock);  // Not necessarily.
      // As the file movement may occur above, we overwrite the entry once more.
      // It doesn't matter.
      entry->entries[*key] = std::make_unique<EntryDesc>();
      entry->entries[*key]->file_size = file.size;
      entry->entries[*key]->last_accessed = file.last_used.time_since_epoch();
    }
  }
}

void DiskCacheEngine::InitializeWorkspaceAt(const std::string& path) {
  auto level = 0;
  std::vector<std::string> dirs{path};

  while (level++ != kSubdirLevel) {
    std::vector<std::string> values;
    for (auto&& e : dirs) {
      for (int i = 0; i != kSubdirs; ++i) {
        auto dir = flare::Format("{}/{}", e, i);
        if (level == kSubdirLevel) {
          entries_per_dir_[dir] = std::make_unique<EntriesInDir>();
        }
        values.push_back(std::move(dir));
      }
    }
    dirs = values;
  }

  for (auto&& e : dirs) {
    Mkdirs(e);
  }
}

std::map<std::string, std::uint64_t> DiskCacheEngine::GetWeightedDirs(
    const std::vector<std::pair<std::string, std::uint64_t>>& directories) {
  std::map<std::string, std::uint64_t> weighted_dirs;
  static constexpr auto kWeightPerDirSize = 7;
  for (auto& [dir, size] : directories) {
    // We convert size to MB by shifting 20 bits on right. Actually we allocate
    // virtual nodes by every 128MB.
    auto weight = size >> 20 >> kWeightPerDirSize;
    if (weight == 0) {
      weight = 1;
    }
    weighted_dirs[dir] = weight;
  }
  return weighted_dirs;
}

std::unordered_map<std::string,
                   std::vector<std::pair<std::string, std::size_t>>>
DiskCacheEngine::GetEntryKeysSnapshot() const {
  std::unordered_map<std::string,
                     std::vector<std::pair<std::string, std::size_t>>>
      snapshot;
  for (auto&& [d, v] : entries_per_dir_) {
    std::scoped_lock __(v->dir_lock);
    for (auto&& [k, entry] : v->entries) {
      std::scoped_lock _(entry->entry_lock);
      snapshot[d].emplace_back(k, entry->file_size);
    }
  }
  return snapshot;
}

std::unordered_map<std::string, std::pair<std::size_t, std::size_t>>
DiskCacheEngine::GetKeyAndByteSizePerDir() const {
  std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> result;
  for (auto&& [d, v] : entries_per_dir_) {
    std::scoped_lock __(v->dir_lock);
    auto total_size =
        std::accumulate(v->entries.begin(), v->entries.end(), 0ULL,
                        [](std::size_t acu, auto&& e) {
                          std::scoped_lock _(e.second->entry_lock);
                          return acu + e.second->file_size;
                        });
    result[d] = std::pair(v->entries.size(), total_size);
  }
  return result;
}

std::vector<std::string> DiskCacheEngine::GetKeys() const {
  std::vector<std::string> result;
  for (auto&& [_, entries] : GetEntryKeysSnapshot()) {
    for (auto&& e : entries) {
      result.emplace_back(std::move(e.first));
    }
  }
  return result;
}

std::optional<flare::NoncontiguousBuffer> DiskCacheEngine::TryGet(
    const std::string& key) const {
  auto path = TryGetPathOfKey(key, true);
  if (!path) {  // `Key` is likely malicious then.
    FLARE_LOG_WARNING_EVERY_SECOND("Failed to map key [{}] to file path.", key);
    return std::nullopt;
  }

  auto dir = GetDirFromPath(*path);
  std::shared_lock<std::shared_mutex> entry_lock;
  flare::Handle fd;

  {
    auto&& dir_entries = entries_per_dir_.at(dir);
    std::shared_lock _(dir_entries->dir_lock);
    if (dir_entries->entries.count(key) == 0) {  // Not there.
      cache_misses_.fetch_add(1, std::memory_order_relaxed);
      return std::nullopt;
    }

    // Open the file.
    auto&& cache_entry = dir_entries->entries.at(key);
    entry_lock = std::shared_lock(cache_entry->entry_lock);
    fd.Reset(open(path->c_str(), O_RDONLY));
    FLARE_PCHECK(fd.Get() != -1);

    // And update its timestamp.
    timespec spec[2] = {{.tv_nsec = UTIME_OMIT} /* atime */,
                        {.tv_nsec = UTIME_NOW} /* mtime */};
    FLARE_PCHECK(futimens(fd.Get(), spec) == 0,
                 "Failed to update `mtime` of the cache.");
    // Even though we only get the shared lock, we update it anyway.
    cache_entry->last_accessed.store(
        std::chrono::system_clock::now().time_since_epoch(),
        std::memory_order_relaxed);
  }

  // Read it into memory. Hopefully it's already in system's page cache.
  flare::NoncontiguousBufferBuilder builder;
  auto status = ReadAppend(fd.Get(), &builder);
  if (status != ReadStatus::Eof) {
    FLARE_LOG_WARNING_EVERY_SECOND("Failed to read cache entry at [{}].",
                                   *path);
    return std::nullopt;
  }
  auto buffer = builder.DestructiveGet();

  // Returning a cache missing can cause overwrite event here. We have change to
  // fix the broken file.
  if (!VerifyEntryAndCutHeader(&buffer)) {
    FLARE_LOG_WARNING("Found corrupted cache entry at [{}].", *path);
    return std::nullopt;
  }

  cache_hits_.fetch_add(1, std::memory_order_relaxed);
  return buffer;  // You're lucky.
}

// Well, writing to (same physical) disk simultaneously from multiple thread
// can be slow. Things would be better if we perform writes in a thread
// dedicated to the destination disk.
//
// For the moment I don't bother doing that, but perhaps we should do it
// sometimes later.
void DiskCacheEngine::Put(const std::string& key,
                          const flare::NoncontiguousBuffer& bytes) {
  auto path = TryGetPathOfKey(key);
  if (!path) {  // `Key` is likely malicious then.
    FLARE_LOG_WARNING_EVERY_SECOND("Failed to map key [{}] to file path.", key);
    return;
  }

  auto&& [handle, dir_lock, entry_lock] = CreateEntryLocked(key, *path);
  if (handle.Get() <= 0) {
    return;
  }

  auto header = WriteFileHeader(bytes);
  FLARE_PCHECK(WriteTo(handle.Get(), header) == header.ByteSize());
  FLARE_PCHECK(WriteTo(handle.Get(), bytes) == bytes.ByteSize());
  auto dir = GetDirFromPath(*path);
  entries_per_dir_.at(dir)->entries[key]->file_size =
      header.ByteSize() + bytes.ByteSize();
  cache_fills_.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> DiskCacheEngine::Purge() {
  std::vector<std::string> purged;
  for (auto&& [path, limit] : dirs_) {
    for (auto&& p : PurgeCacheAt(path, limit)) {
      purged.emplace_back(std::move(p));
    }
  }
  return purged;
}

Json::Value DiskCacheEngine::DumpInternals() const {
  Json::Value jsv;
  auto&& snapshot = GetEntryKeysSnapshot();
  {
    auto&& stat = jsv["statistics"];
    stat["fills"] =
        static_cast<Json::UInt64>(cache_fills_.load(std::memory_order_relaxed));
    stat["hits"] =
        static_cast<Json::UInt64>(cache_hits_.load(std::memory_order_relaxed));
    stat["misses"] = static_cast<Json::UInt64>(
        cache_misses_.load(std::memory_order_relaxed));
    stat["overwrites"] = static_cast<Json::UInt64>(
        cache_overwrites_.load(std::memory_order_relaxed));
  }

  auto&& partitions = jsv["partitions"];
  auto total_entries = 0;
  auto profile_per_dir = GetKeyAndByteSizePerDir();
  std::unordered_map<std::string, std::pair<std::size_t, std::size_t>>
      dir_key_counter;
  // Dir path must not be the prefix of each other.
  std::for_each(profile_per_dir.begin(), profile_per_dir.end(), [&](auto&& e) {
    for (auto&& d : dirs_) {
      if (flare::StartsWith(e.first, d.first)) {
        dir_key_counter[d.first].first += e.second.first;
        dir_key_counter[d.first].second += e.second.second;
        break;
      }
    }
  });

  for (auto&& e : dirs_) {
    auto&& dir = partitions[e.first];
    dir["capacity_in_bytes"] = static_cast<Json::UInt64>(e.second);
    dir["hits"] = static_cast<Json::UInt64>(
        shard_hits_.at(e.first)->load(std::memory_order_relaxed));
    dir["entries"] = static_cast<Json::UInt64>(dir_key_counter[e.first].first);
    dir["used_in_bytes"] =
        static_cast<Json::UInt64>(dir_key_counter[e.first].second);
    total_entries += dir_key_counter[e.first].first;
  }
  partitions["total_entries"] = static_cast<Json::UInt64>(total_entries);
  return jsv;
}

std::tuple<flare::Handle, std::unique_lock<std::shared_mutex>,
           std::unique_lock<std::shared_mutex>>
DiskCacheEngine::CreateEntryLocked(const std::string& key,
                                   const std::string& path) {
  auto dir = GetDirFromPath(path);
  auto&& dir_entries = entries_per_dir_.at(dir);
  std::unique_lock dir_lock(dir_entries->dir_lock);
  if (dir_entries->entries.count(key) == 0) {
    dir_entries->entries[key] = std::make_unique<EntryDesc>();
    dir_entries->entries[key]->last_accessed.store(
        std::chrono::system_clock().now().time_since_epoch());
  } else {
    cache_overwrites_.fetch_add(1, std::memory_order_relaxed);
  }
  std::unique_lock entry_lock(dir_entries->entries[key]->entry_lock);
  auto handle =
      flare::Handle(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
  // If creating file failed, we must erase the entry. Otherwise we have no
  // chance to remove the entry, this will cause memory leaking.
  if (handle.Get() == -1) {
    // FIXME: What about I/O failure?
    //
    // For the moment we just print a warning and ignore that. This isn't a
    // correctness issue, as our cache isn't persistent anyway. Yet it does
    // hurt performance if it occurs too often.
    FLARE_LOG_WARNING("Failed to create file [{}]. [{}]: {}", path, errno,
                      strerror(errno));
    dir_entries->entries.erase(key);
    return {};
  }
  return std::tuple(std::move(handle), std::move(dir_lock),
                    std::move(entry_lock));
}

std::optional<std::string> DiskCacheEngine::TryGetPathOfKey(
    const std::string& key, bool record) const {
  auto marshalled = MarshalKey(key);
  if (marshalled.size() > PATH_MAX) {
    FLARE_LOG_WARNING_EVERY_SECOND("Unexpected key [{}].", key);
    return std::nullopt;
  }
  auto hash = XxHash()(key);
  auto subdirs = GetSubdirsFor(hash);
  std::string result = shard_mapper_.GetNode(hash);
  if (record) {
    // Statistics.
    shard_hits_.at(result)->fetch_add(1, std::memory_order_relaxed);
  }

  for (auto&& e : subdirs) {
    result += flare::Format("/{}", e);
  }
  result += flare::Format("/{}", marshalled);

  return result;
}

// Simply enumerate all files inside `path` and remove some old entries. We also
// remove meta info. about the removed entries from `entries_`.
std::vector<std::string> DiskCacheEngine::PurgeCacheAt(
    const std::string& path, std::uint64_t size_limit) {
  constexpr auto kDiscardThreshold = 0.95;

  std::vector<std::string> purged;

  // Enumerate all files in our workspace.
  auto files = EnumerateCacheEntries(path);

  // Sort by last used timestamp.
  std::sort(files.begin(), files.end(),
            [](auto&& x, auto&& y) { return x.last_used < y.last_used; });

  // Total bytes used.
  std::uint64_t total_used = 0;
  for (auto&& e : files) {
    total_used += e.size;
  }

  for (auto&& e : files) {
    if (total_used < size_limit * kDiscardThreshold) {
      return purged;
    }
    flare::ScopedDeferred __([=, &total_used]() { total_used -= e.size; });
    // Remove the file and it's meta info.
    auto key = GetKeyFromPath(e.path);
    auto path = GetDirFromPath(e.path);
    if (key) {
      if (entries_per_dir_.count(path)) {
        auto&& dir_entry = entries_per_dir_.at(path);
        std::scoped_lock _(dir_entry->dir_lock);
        if (dir_entry->entries.count(*key)) {
          FLARE_PCHECK(unlink(e.path.c_str()) == 0, "Failed to remove [{}].",
                       e.path);
          dir_entry->entries.erase(*key);
          purged.emplace_back(std::move(*key));
          continue;
        }
      }
    } else {
      FLARE_LOG_WARNING("Unrecognized file name pattern: {}", e.path);
    }
    FLARE_PCHECK(unlink(e.path.c_str()) == 0, "Failed to remove [{}].", e.path);
  }
  return purged;
}

FLARE_REGISTER_CLASS_DEPENDENCY(cache_engine_registry, "disk", DiskCacheEngine);

}  // namespace yadcc::cache
