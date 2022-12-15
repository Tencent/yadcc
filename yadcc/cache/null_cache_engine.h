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

#ifndef YADCC_CACHE_NULL_CACHE_ENGINE_H_
#define YADCC_CACHE_NULL_CACHE_ENGINE_H_

#include <optional>
#include <string>
#include <vector>

#include "jsoncpp/value.h"

#include "flare/base/buffer.h"

#include "yadcc/cache/cache_engine.h"

namespace yadcc::cache {

// We use this cache engine to indicate that we only have l1 memory cache. All
// the implements of the interfaces are empty.
class NullCacheEngine : public CacheEngine {
 public:
  std::vector<std::string> GetKeys() const override;

  std::optional<flare::NoncontiguousBuffer> TryGet(
      const std::string& key) const override;

  void Put(const std::string& key,
           const flare::NoncontiguousBuffer& bytes) override;

  void Purge() override;

  Json::Value DumpInternals() const override;
};

}  // namespace yadcc::cache

#endif  // YADCC_CACHE_NULL_CACHE_ENGINE_H_
