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

#ifndef YADCC_DAEMON_CACHE_FORMAT_H_
#define YADCC_DAEMON_CACHE_FORMAT_H_

#include <optional>
#include <string>
#include <string_view>

#include "flare/base/buffer.h"

#include "yadcc/api/env_desc.pb.h"

namespace yadcc::daemon {

struct CacheEntry {
  int exit_code;
  std::string standard_output;
  std::string standard_error;
  flare::NoncontiguousBuffer object_file;
};

// Generate a Redis key for compilation cache.
std::string GetCacheEntryKey(const EnvironmentDesc& desc,
                             const std::string& invocation_arguments,
                             const std::string_view& source_digest);

// Serializes a compilation result.
flare::NoncontiguousBuffer WriteCacheEntry(const CacheEntry& result);

// Deserialize a compilation result that was serialized by the method above.
std::optional<CacheEntry> TryParseCacheEntry(flare::NoncontiguousBuffer buffer);

}  // namespace yadcc::daemon

#endif  // YADCC_DAEMON_CACHE_FORMAT_H_
