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

#include "yadcc/cache/null_cache_engine.h"

namespace yadcc::cache {

std::vector<std::string> NullCacheEngine::GetKeys() const {
  return std::vector<std::string>();
}

std::optional<flare::NoncontiguousBuffer> NullCacheEngine::TryGet(
    const std::string& key) const {
  return std::nullopt;
}

void NullCacheEngine::Put(const std::string& key,
                          const flare::NoncontiguousBuffer& bytes) {}

void NullCacheEngine::Purge() {}

Json::Value NullCacheEngine::DumpInternals() const { return Json::Value(); }

FLARE_REGISTER_CLASS_DEPENDENCY(cache_engine_registry, "null", NullCacheEngine);

}  // namespace yadcc::cache
