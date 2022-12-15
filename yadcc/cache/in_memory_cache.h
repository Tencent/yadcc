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

#ifndef YADCC_CACHE_IN_MEMORY_CACHE_H_
#define YADCC_CACHE_IN_MEMORY_CACHE_H_

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "jsoncpp/value.h"

#include "flare/base/buffer.h"

namespace yadcc::cache {

// Implement of ARC algoritm.
// @sa:
// https://www.usenix.org/legacy/events/fast03/tech/full_papers/megiddo/megiddo.pdf
//
// Our implementation is a little different from the paper, mainly because the
// size of the cached pages in the paper is fixed.
class InMemoryCache {
 public:
  // We must supply the max byte size we want the cache hold.
  explicit InMemoryCache(std::size_t max_size);

  bool Put(const std::string& key, const flare::NoncontiguousBuffer& buffer);
  std::optional<flare::NoncontiguousBuffer> TryGet(const std::string& key);
  void Remove(const std::vector<std::string>& keys);
  std::vector<std::string> GetKeys() const;

  Json::Value DumpInternals() const;

 private:
  typedef std::list<std::pair<std::string, std::size_t>>::iterator
      EntryIterator;

  struct CacheList {
    std::size_t size = 0;
    std::list<std::pair<std::string, std::size_t>> list;
  };

  struct CacheEntry {
    flare::NoncontiguousBuffer buffer;
    CacheList* belonging_list;
    EntryIterator entry_iter;
  };

 private:
  // Replace the content of the entry which is already in our actaul cache list.
  void UnsafeOverwrite(const std::string& key,
                       const flare::NoncontiguousBuffer& buffer);

  // Transfer the entry from ont cache list to another.
  void UnsafePutEntryIntoList(const std::string& key, CacheEntry* entry,
                              CacheList* dst_list);

  // Cache the entry if entry is in phantom lists.
  void UnsafeCacheInPhantom(int phantom_index, const std::string& key,
                            CacheEntry* entry);

  // Cache the entry if entry all miss.
  void UnsafeCacheIfMiss(const std::string& key, CacheEntry* entry);

  // Adaptive adjustment of the cache pattern among LRU and LFU.
  void UnsafeAdaptiveAdjust(int phantom_index);

  // Try to get enough space from the cache list to add new entry. If is not
  // enough, return the remaining size.
  std::size_t UnsafeTryEvict(CacheList* cache_list,
                             std::size_t desired_byte_size, bool in_memory);

  // If memory overflows, we should evict some entries.
  void EvictMemoryOverflow();

  // Evict entries in once list to phantom.
  void EvictHitOnceToPhantom();

  // Evict entries in more than once list to phantom.
  void EvictMoreThanOnceToPhantom();

 private:
  const std::size_t max_size_in_bytes_;

  std::atomic<std::uint64_t> hits_{}, misses_{};

  // This variable describes the size of the T1 cache and varies with the cache
  // hit pattern.  The larger the variable, the closer to LRU. Otherwise, the
  // closer to LFU.
  std::size_t adaptive_size_of_once_{0};

  // The Cache list store entries which hit exactly once.
  CacheList list_hit_once_;
  // The Cache list store entries which evcited from t1.
  CacheList list_hit_once_phantom_;
  // The Cache list store entries which hit more than once.
  CacheList list_more_than_once_;
  // The Cache list store entries which evcited from t2.
  CacheList list_more_than_once_phantom_;

  mutable std::mutex lock_;
  std::unordered_map<std::string, CacheEntry> memory_buffer_mapper_;

  // The first field in pair represents the index of the phantom list, and then
  // second describe the iterator where the entry is in.
  std::unordered_map<std::string, std::pair<int, EntryIterator>>
      phantom_entry_mapper_;
};

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_IN_MEMORY_CACHE_H_
