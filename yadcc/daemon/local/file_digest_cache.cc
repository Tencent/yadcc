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

#include "yadcc/daemon/local/file_digest_cache.h"

#include <mutex>

#include "flare/base/logging.h"
#include "flare/base/never_destroyed.h"

namespace yadcc::daemon::local {

FileDigestCache::FileDigestCache()
    : internal_exposer_("yadcc/file_digests",
                        [this] { return DumpInternals(); }) {}

FileDigestCache* FileDigestCache::Instance() {
  static flare::NeverDestroyed<FileDigestCache> instance;
  return instance.Get();
}

std::optional<std::string> FileDigestCache::TryGet(const std::string& path,
                                                   std::uint64_t size,
                                                   std::uint64_t mtime) const {
  FLARE_CHECK(!path.empty() && path[0] == '/', "Absolute path is required.");
  std::shared_lock _(lock_);

  if (auto iter = digests_.find(path); iter != digests_.end() &&
                                       iter->second.size == size &&
                                       iter->second.mtime == mtime) {
    return iter->second.hash;
  }
  return std::nullopt;
}

void FileDigestCache::Set(const std::string& path, std::uint64_t size,
                          std::uint64_t mtime, std::string hash) {
  std::scoped_lock _(lock_);
  digests_[path] = Desc{.size = size, .mtime = mtime, .hash = hash};
}

Json::Value FileDigestCache::DumpInternals() const {
  std::scoped_lock _(lock_);
  Json::Value jsv;

  for (auto&& [k, v] : digests_) {
    auto&& entry = jsv[k];
    entry["size"] = static_cast<Json::UInt64>(v.size);
    entry["mtime"] = static_cast<Json::UInt64>(v.mtime);
    entry["digest"] = v.hash;
  }
  return jsv;
}

}  // namespace yadcc::daemon::local
