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

#ifndef YADCC_DAEMON_LOCAL_FILE_DIGEST_CACHE_H_
#define YADCC_DAEMON_LOCAL_FILE_DIGEST_CACHE_H_

#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "jsoncpp/value.h"

#include "flare/base/exposed_var.h"

namespace yadcc::daemon::local {

// Caches file and its (in whatever fashion) derived hash.
//
// In certain cases, we need hash of some file that is not accessible to us (as
// we might not be running as the same user as our client). However, hashing a
// (possibly) large file can be slow and therefore we can't afford doing that.
//
// Therefore, we maintain an in-memory cache to save the hashes. For files we've
// seen before, we only need its (path, size, mtime), which is cheap to obtain,
// to lookup their hash.
//
// This is a volatile cache, each time we're restarted, it needs to be refilled
// (with the help of our client).
class FileDigestCache {
 public:
  FileDigestCache();

  static FileDigestCache* Instance();

  // See if the file digest is cached by us. `path` must be absolute path.
  std::optional<std::string> TryGet(const std::string& path, std::uint64_t size,
                                    std::uint64_t mtime) const;

  // Save hash of a file. `path` must be absolute path.
  void Set(const std::string& path, std::uint64_t size, std::uint64_t mtime,
           std::string hash);

  Json::Value DumpInternals() const;

 private:
  struct Desc {
    std::uint64_t size;
    std::uint64_t mtime;
    std::string hash;
  };

  flare::ExposedVarDynamic<Json::Value> internal_exposer_;

  mutable std::shared_mutex lock_;
  // Indexed by path. For each path we only store its last-seen size/mtime/... .
  // In case the file is changed, there's hardly a point in storing the file's
  // past-life information.
  std::unordered_map<std::string, Desc> digests_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_FILE_DIGEST_CACHE_H_
