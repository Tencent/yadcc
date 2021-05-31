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

#ifndef YADCC_CACHE_CACHE_ENGINE_H_
#define YADCC_CACHE_CACHE_ENGINE_H_

#include <optional>
#include <string>
#include <vector>

#include "thirdparty/jsoncpp/value.h"

#include "flare/base/buffer.h"
#include "flare/base/dependency_registry.h"

namespace yadcc::cache {

// The Abstract base class to define the interfaces which cache implements
// should comply to.
class CacheEngine {
 public:
  virtual ~CacheEngine() = default;

  // Get all of the keys the cache will hold.
  virtual std::vector<std::string> GetKeys() const = 0;

  // Try get the entry of the key, if cache hit.
  virtual std::optional<flare::NoncontiguousBuffer> TryGet(
      const std::string& key) const = 0;

  // Put an entry of the key into cache.
  virtual void Put(const std::string& key,
                   const flare::NoncontiguousBuffer& bytes) = 0;

  // Purge function. Return the keys purged.
  virtual std::vector<std::string> Purge() = 0;

  // Dumps internal about this cache engine.
  virtual Json::Value DumpInternals() const = 0;
};

FLARE_DECLARE_CLASS_DEPENDENCY_REGISTRY(cache_engine_registry, CacheEngine);

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_CACHE_ENGINE_H_
