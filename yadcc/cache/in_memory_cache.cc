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

#include <algorithm>
#include <utility>
#include <vector>

#include "flare/base/string.h"

namespace yadcc::cache {

namespace {

// As we're using 64K (Flare) buffer blocks, if the buffer block is not fully
// filled, we risk wasting a lot of space.
//
// This method repacks `buffer` with a tightly-fit buffer, mitigating this
// issue.
flare::NoncontiguousBuffer CompactBuffer(
    const flare::NoncontiguousBuffer& buffer) {
  auto flatten = flare::FlattenSlow(buffer);
  flare::NoncontiguousBuffer result;
  result.Append(flare::MakeForeignBuffer(std::move(flatten)));
  return result;
}

}  // namespace

InMemoryCache::InMemoryCache(std::size_t max_size)
    : max_size_in_bytes_(max_size) {}

bool InMemoryCache::Put(const std::string& key,
                        const flare::NoncontiguousBuffer& buffer) {
  if (buffer.ByteSize() > max_size_in_bytes_) {
    return false;
  }
  auto reshaped_buffer = CompactBuffer(buffer);

  std::scoped_lock _(lock_);

  // If the entry is already in our cache(means in t1 or t2), we replace the
  // content of the entry simplely.
  if (memory_buffer_mapper_.count(key)) {
    UnsafeOverwrite(key, reshaped_buffer);
    return true;
  }

  CacheEntry entry = {reshaped_buffer, nullptr};

  auto entry_in_phantom = phantom_entry_mapper_.find(key);
  // The case the entry is in phantom list. We push it back into t1 or t2 cache
  // list.
  if (entry_in_phantom != phantom_entry_mapper_.end()) {
    entry.entry_iter = entry_in_phantom->second.second;
    if (entry_in_phantom->second.first == 1) {
      entry.belonging_list = &list_hit_once_phantom_;
    } else {
      entry.belonging_list = &list_more_than_once_phantom_;
    }
    UnsafeCacheInPhantom(entry_in_phantom->second.first, key, &entry);
  } else {
    // The case the entry miss all the cache lists. We push it into t1 cache
    // list.
    UnsafeCacheIfMiss(key, &entry);
  }
  EvictMemoryOverflow();
  return true;
}

// If the entry hit the t1 or t2 cache, we can return the cache. Then we move
// the entry to the top of t2 list.
std::optional<flare::NoncontiguousBuffer> InMemoryCache::TryGet(
    const std::string& key) {
  std::scoped_lock _(lock_);
  if (memory_buffer_mapper_.count(key) == 0) {
    misses_.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
  }

  UnsafePutEntryIntoList(key, &memory_buffer_mapper_.at(key),
                         &list_more_than_once_);
  hits_.fetch_add(1, std::memory_order_relaxed);
  return memory_buffer_mapper_.at(key).buffer;
}

void InMemoryCache::Remove(const std::vector<std::string>& keys) {
  std::scoped_lock _(lock_);
  for (const auto& key : keys) {
    if (memory_buffer_mapper_.count(key) == 0) {
      continue;
    }
    auto iter = memory_buffer_mapper_.find(key);
    FLARE_CHECK(iter != memory_buffer_mapper_.end());
    iter->second.belonging_list->size -= iter->second.buffer.ByteSize();
    iter->second.belonging_list->list.remove_if(
        [&](const std::pair<std::string, std::size_t>& e) {
          return e.first == key;
        });
    memory_buffer_mapper_.erase(key);
  }
}

std::vector<std::string> InMemoryCache::GetKeys() const {
  std::vector<std::string> keys;
  std::scoped_lock _(lock_);
  for (auto&& [k, _] : memory_buffer_mapper_) {
    keys.emplace_back(k);
  }
  return keys;
}

Json::Value InMemoryCache::DumpInternals() const {
  Json::Value jsv;

  {
    std::scoped_lock _(lock_);
    jsv["actual_size_in_bytes"] = static_cast<Json::UInt64>(
        list_hit_once_.size + list_more_than_once_.size);
    jsv["actual_entries"] = static_cast<Json::UInt64>(
        list_hit_once_.list.size() + list_more_than_once_.list.size());
    jsv["phantom_size_in_bytes"] = static_cast<Json::UInt64>(
        list_hit_once_phantom_.size + list_more_than_once_phantom_.size);
    jsv["phantom_entries"] =
        static_cast<Json::UInt64>(list_hit_once_phantom_.list.size() +
                                  list_more_than_once_phantom_.list.size());
  }

  jsv["hits"] =
      static_cast<Json::UInt64>(hits_.load(std::memory_order_relaxed));
  jsv["misses"] =
      static_cast<Json::UInt64>(misses_.load(std::memory_order_relaxed));
  return jsv;
}

void InMemoryCache::UnsafeOverwrite(const std::string& key,
                                    const flare::NoncontiguousBuffer& buffer) {
  auto iter = memory_buffer_mapper_.find(key);
  FLARE_CHECK(iter != memory_buffer_mapper_.end());
  iter->second.buffer = buffer;
  auto origin_size = iter->second.entry_iter->second;
  iter->second.belonging_list->size -= origin_size;
  iter->second.entry_iter->second = buffer.ByteSize();
  iter->second.belonging_list->size += iter->second.entry_iter->second;
  if (buffer.ByteSize() > origin_size) {
    EvictMemoryOverflow();
  }
}

void InMemoryCache::UnsafeCacheInPhantom(int phantom_index,
                                         const std::string& key,
                                         CacheEntry* entry) {
  // To update the adaptive variable according to which phantom list the entry
  // come from. If the entry come from b1, a increment of the variable make
  // cache pattern closer to LRU. Otherwise, closer to LFU.
  auto buffer_size = entry->buffer.ByteSize();
  if (phantom_index == 1) {
    double ratio =
        list_hit_once_phantom_.size < list_more_than_once_phantom_.size
            ? static_cast<double>(list_more_than_once_phantom_.size) /
                  list_hit_once_phantom_.size
            : 1;
    adaptive_size_of_once_ += buffer_size * ratio;
    adaptive_size_of_once_ =
        std::min(adaptive_size_of_once_, max_size_in_bytes_);
  } else {
    double ratio =
        list_more_than_once_phantom_.size < list_hit_once_phantom_.size
            ? static_cast<double>(list_hit_once_phantom_.size) /
                  list_more_than_once_phantom_.size
            : 1;
    buffer_size *= ratio;
    adaptive_size_of_once_ = adaptive_size_of_once_ > buffer_size
                                 ? adaptive_size_of_once_ - buffer_size
                                 : 0;
  }

  UnsafeAdaptiveAdjust(phantom_index);
  UnsafePutEntryIntoList(key, entry, &list_more_than_once_);
  memory_buffer_mapper_.insert(std::pair{key, std::move(*entry)});
  phantom_entry_mapper_.erase(key);
}

void InMemoryCache::UnsafeCacheIfMiss(const std::string& key,
                                      CacheEntry* entry) {
  auto remaining_size = entry->buffer.ByteSize();
  if (list_hit_once_.size + list_more_than_once_.size + remaining_size >
      max_size_in_bytes_) {
    if (list_hit_once_.size + list_hit_once_phantom_.size + remaining_size >
        max_size_in_bytes_) {
      // Evict b1 first.
      if (list_hit_once_phantom_.size > 0) {
        remaining_size =
            UnsafeTryEvict(&list_hit_once_phantom_, remaining_size, false);
      }
      // If b1 is not big enough, continue to evict the t1.
      if (remaining_size) {
        remaining_size = UnsafeTryEvict(&list_hit_once_, remaining_size, true);
      }
    }
    if (remaining_size) {
      if (list_hit_once_.size + list_more_than_once_.size +
              list_hit_once_phantom_.size + list_more_than_once_phantom_.size +
              remaining_size >
          max_size_in_bytes_) {
        if (list_hit_once_.size + list_more_than_once_.size +
                list_hit_once_phantom_.size +
                list_more_than_once_phantom_.size + remaining_size >=
            2 * max_size_in_bytes_) {
          // Evict b2 first.
          if (list_more_than_once_phantom_.size > 0) {
            remaining_size = UnsafeTryEvict(&list_more_than_once_phantom_,
                                            remaining_size, false);
          }
          // If b2 is not big enough, continue to evict the t2.
          if (remaining_size) {
            remaining_size =
                UnsafeTryEvict(&list_more_than_once_, remaining_size, true);
          }
        } else {
          UnsafeAdaptiveAdjust(1);
        }
      }
    }
  }
  // Finally fetch entry to the cache and move it to MRU position in T1.
  UnsafePutEntryIntoList(key, entry, &list_hit_once_);
  memory_buffer_mapper_.insert(std::pair{key, std::move(*entry)});
}

void InMemoryCache::UnsafePutEntryIntoList(const std::string& key,
                                           CacheEntry* entry,
                                           CacheList* dst_list) {
  // Remove the entry from source list.
  if (entry->belonging_list) {
    FLARE_CHECK(entry->entry_iter != entry->belonging_list->list.end());
    // Subtract the size of the entry from the source list.
    if (phantom_entry_mapper_.count(key)) {
      entry->belonging_list->size -=
          phantom_entry_mapper_.at(key).second->second;
    } else {
      entry->belonging_list->size -= entry->entry_iter->second;
    }
    entry->belonging_list->list.erase(entry->entry_iter);
    entry->entry_iter = entry->belonging_list->list.end();
  }
  // Push the entry to destination list.
  std::size_t size = entry->buffer.ByteSize();
  dst_list->list.emplace_front(key, size);
  dst_list->size += entry->buffer.ByteSize();
  entry->belonging_list = dst_list;
  entry->entry_iter = entry->belonging_list->list.begin();
}

std::size_t InMemoryCache::UnsafeTryEvict(CacheList* cache_list,
                                          std::size_t desired_byte_size,
                                          bool in_memory) {
  while (desired_byte_size && cache_list->size) {
    auto remove_size = cache_list->list.back().second;
    cache_list->size -= remove_size;
    desired_byte_size =
        desired_byte_size > remove_size ? desired_byte_size - remove_size : 0;
    if (in_memory) {
      memory_buffer_mapper_.erase(cache_list->list.back().first);
    } else {
      phantom_entry_mapper_.erase(cache_list->list.back().first);
      UnsafeAdaptiveAdjust(1);
    }
    cache_list->list.pop_back();
  }
  return desired_byte_size;
}

void InMemoryCache::EvictMemoryOverflow() {
  while (list_hit_once_.size + list_more_than_once_.size > max_size_in_bytes_) {
    if (list_hit_once_.size > adaptive_size_of_once_) {
      EvictHitOnceToPhantom();
    } else {
      EvictMoreThanOnceToPhantom();
    }
  }
  while (list_hit_once_.size + list_hit_once_phantom_.size >
         max_size_in_bytes_) {
    auto remove_size = list_hit_once_phantom_.list.back().second;
    list_hit_once_phantom_.size -= remove_size;
    phantom_entry_mapper_.erase(list_hit_once_phantom_.list.back().first);
    list_hit_once_phantom_.list.pop_back();
  }

  while (list_more_than_once_.size + list_more_than_once_phantom_.size >
         max_size_in_bytes_) {
    auto remove_size = list_more_than_once_phantom_.list.back().second;
    list_more_than_once_phantom_.size -= remove_size;
    phantom_entry_mapper_.erase(list_more_than_once_phantom_.list.back().first);
    list_more_than_once_phantom_.list.pop_back();
  }
}

void InMemoryCache::EvictHitOnceToPhantom() {
  auto key = list_hit_once_.list.back().first;
  auto&& entry = memory_buffer_mapper_.at(key);
  UnsafePutEntryIntoList(key, &entry, &list_hit_once_phantom_);
  phantom_entry_mapper_[key] = std::pair(1, entry.entry_iter);
  memory_buffer_mapper_.erase(key);
}

void InMemoryCache::EvictMoreThanOnceToPhantom() {
  auto key = list_more_than_once_.list.back().first;
  auto&& entry = memory_buffer_mapper_.at(key);
  UnsafePutEntryIntoList(key, &entry, &list_more_than_once_phantom_);
  phantom_entry_mapper_[key] = std::pair(2, entry.entry_iter);
  memory_buffer_mapper_.erase(key);
}

void InMemoryCache::UnsafeAdaptiveAdjust(int phantom_index) {
  if (list_hit_once_.size > adaptive_size_of_once_ ||
      (phantom_index == 2 && list_hit_once_.size >= adaptive_size_of_once_)) {
    if (list_hit_once_.size) {
      EvictHitOnceToPhantom();
    }
  } else {
    auto adaptive_size_of_t2 = max_size_in_bytes_ - adaptive_size_of_once_;
    if (list_more_than_once_.size &&
        list_more_than_once_.size >= adaptive_size_of_t2) {
      EvictMoreThanOnceToPhantom();
    }
  }
}

}  // namespace yadcc::cache
