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

#ifndef YADCC_CACHE_COS_CACHE_ENGINE_H_
#define YADCC_CACHE_COS_CACHE_ENGINE_H_

#include <optional>
#include <string>
#include <vector>

#include "flare/base/buffer.h"
#include "flare/base/future.h"
#include "flare/net/cos/cos_client.h"

#include "yadcc/cache/cache_engine.h"

namespace yadcc::cache {

// This cache engine stores entries in Tencent Cloud COS.
class CosCacheEngine : public CacheEngine {
 public:
  CosCacheEngine();

  std::vector<std::string> GetKeys() const override;

  std::optional<flare::NoncontiguousBuffer> TryGet(
      const std::string& key) const override;

  void Put(const std::string& key,
           const flare::NoncontiguousBuffer& bytes) override;

  void Purge() override;

  Json::Value DumpInternals() const override;

 private:
  struct EntryDesc {
    std::string key;  // Not prefixed with COS-related prefixes.
    std::chrono::system_clock::time_point timestamp;
    std::uint64_t size;
  };

  // Get all entries in `subdir` (from root).
  std::vector<EntryDesc> GetEntriesIn(const std::string& subdir) const;

  // Get all entries.
  std::vector<EntryDesc> GetEntries() const;

 private:
  std::uint64_t capacity_;
  flare::CosClient client_;

  // To reduce API calls to COS (APIs are charged per call), we do not rescan
  // entries in `Purge()`. Instead, when `GetKeys()` is called, we scan the
  // object list to see if some of them should be removed, and save the list
  // here. Later when `Purge()` is called, actual removal is done.
  //
  // Note that keys here is NOT prefixed with COS-related prefixes.
  mutable std::mutex lock_;
  mutable std::vector<std::string> pending_removal_;
};

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_COS_CACHE_ENGINE_H_
