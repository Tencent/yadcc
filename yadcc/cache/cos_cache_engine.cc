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

#include "yadcc/cache/cos_cache_engine.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

#include "gflags/gflags.h"

#include "flare/base/string.h"
#include "flare/fiber/async.h"
#include "flare/fiber/this_fiber.h"
#include "flare/net/cos/ops/bucket/get_bucket.h"
#include "flare/net/cos/ops/object/delete_multiple_objects.h"
#include "flare/net/cos/ops/object/get_object.h"
#include "flare/net/cos/ops/object/put_object.h"

#include "yadcc/common/parse_size.h"
#include "yadcc/common/xxhash.h"

// COS-specific configurations.
DEFINE_string(cos_engine_secret_id, "", "SecretId for accessing COS.");
DEFINE_string(cos_engine_secret_key, "", "SecretKey for accessing COS.");
DEFINE_string(cos_engine_bucket, "", "COS bucket for storing cache entries.");

// Determines which COS AP should we connect to.
DEFINE_string(
    cos_engine_cos_uri, "",
    "URI to COS server. Usually it's something like `cos://ap-xxxx` (region).");

// yadcc-cache configurations.
DEFINE_string(cos_engine_dir, "yadcc-cache",
              "You can specify a directory name here so that yadcc-cache won't "
              "put all entries in the root directory.");
DEFINE_string(cos_engine_capacity, "10G",
              "A rough upper limit on how many bytes can be used for caching.");

using namespace std::literals;

namespace yadcc::cache {

constexpr auto kSubDirs = 128;

namespace {

std::string MakeObjectKey(const std::string_view& key) {
  return flare::Format("{}/{}/{}", FLAGS_cos_engine_dir,
                       XxHash{}(key) % kSubDirs, key);
}

// Dealing with time zone is hard, so we keep whatever timezone is used by COS
// (UTC, usually.).
std::optional<std::chrono::system_clock::time_point> FromIso8601Timestamp(
    const std::string& str) {
  constexpr auto kExpectSize = "2020-12-10T03:37:30.000Z"sv.size();
  if (str.size() != kExpectSize) {
    return std::nullopt;
  }

  // Text:   2020-12-10T03:37:30.000Z
  // Offset: 0123 56 89 12 45 78 012
  std::string_view view = str;
  auto year = flare::TryParse<std::uint32_t>(view.substr(0, 4));
  auto mon = flare::TryParse<std::uint32_t>(view.substr(5, 2));
  auto day = flare::TryParse<std::uint32_t>(view.substr(8, 2));
  auto hour = flare::TryParse<std::uint32_t>(view.substr(11, 2));
  auto min = flare::TryParse<std::uint32_t>(view.substr(14, 2));
  auto sec = flare::TryParse<std::uint32_t>(view.substr(17, 2));
  if (!year || !mon || !day || !hour || !min || !sec) {
    return std::nullopt;
  }

  std::tm time = {};
  time.tm_year = *year;
  time.tm_mon = *mon;
  time.tm_mday = *day;
  time.tm_hour = *hour;
  time.tm_min = *min;
  time.tm_sec = *sec;
  time.tm_isdst = 0;
  return std::chrono::system_clock::from_time_t(mktime(&time));
}

}  // namespace

CosCacheEngine::CosCacheEngine() {
  flare::CosClient::Options opts = {.secret_id = FLAGS_cos_engine_secret_id,
                                    .secret_key = FLAGS_cos_engine_secret_key,
                                    .bucket = FLAGS_cos_engine_bucket};
  FLARE_CHECK(client_.Open(FLAGS_cos_engine_cos_uri, opts),
              "Failed to open COS URI.");

  auto size = TryParseSize(FLAGS_cos_engine_capacity);
  FLARE_CHECK(size, "Invalid size specified to `cos_engine_capacity`.");
  capacity_ = *size;
}

std::vector<std::string> CosCacheEngine::GetKeys() const {
  auto entries = GetEntries();
  std::vector<std::string> result;
  for (auto&& e : entries) {
    result.push_back(e.key);
  }
  return result;
}

std::optional<flare::NoncontiguousBuffer> CosCacheEngine::TryGet(
    const std::string& key) const {
  flare::CosGetObjectRequest req;
  req.key = MakeObjectKey(key);
  auto result = client_.Execute(req);
  if (result) {
    return result->bytes;
  }
  return std::nullopt;
}

void CosCacheEngine::Put(const std::string& key,
                         const flare::NoncontiguousBuffer& bytes) {
  flare::CosPutObjectRequest req;
  req.key = MakeObjectKey(key);
  req.bytes = bytes;
  if (auto result = client_.Execute(req); !result) {
    FLARE_LOG_WARNING_EVERY_SECOND("Failed to save {} bytes into COS: {}",
                                   bytes.ByteSize(), result.error().ToString());
  }
}

void CosCacheEngine::Purge() {
  constexpr auto kBatchSize = 1000;  // Maximum allowed by COS.
  std::vector<std::string> keys;
  {
    std::scoped_lock _(lock_);
    keys.swap(pending_removal_);
  }

  std::size_t purged = 0;
  for (auto iter = keys.begin(); iter != keys.end();) {
    flare::CosDeleteMultipleObjectsRequest req;
    while (iter != keys.end() && req.objects.size() < kBatchSize) {
      req.objects.emplace_back().key = MakeObjectKey(*iter++);
    }
    if (auto result = client_.Execute(req)) {
      purged += req.objects.size();
      FLARE_VLOG(10, "Purged {} entries.", req.objects.size());
    } else {
      FLARE_LOG_WARNING_EVERY_SECOND(
          "Failed to purge out some obsolete objects. We'll try again later: "
          "{}",
          result.error().ToString());
    }
  }
  FLARE_VLOG(1, "Purged {} entries from COS cache.", purged);
}

Json::Value CosCacheEngine::DumpInternals() const {
  auto entries = GetEntries();
  std::uint64_t total_size = 0;
  for (auto&& e : entries) {
    total_size += e.size;
  }

  Json::Value result;
  result["entries"] = static_cast<Json::UInt64>(entries.size());
  result["total_size_in_bytes"] = static_cast<Json::UInt64>(total_size);
  return result;
}

std::vector<CosCacheEngine::EntryDesc> CosCacheEngine::GetEntriesIn(
    const std::string& subdir) const {
  constexpr auto kMaxEntries = 1048576;

  std::string marker;
  std::vector<CosCacheEngine::EntryDesc> entries;

  while (true) {
    flare::CosGetBucketRequest req;
    req.marker = marker;
    req.prefix = subdir;

    auto result = client_.Execute(req);
    for (int i = 0; i != 3 && !result; ++i) {
      flare::this_fiber::SleepFor(100ms);
      // Retry on failure. I do expect a small but non-eligible failure rate
      // when calling COS.
      result = client_.Execute(req);
    }
    if (!result) {
      FLARE_LOG_WARNING_EVERY_SECOND(
          "Failed to enumerate all files in [{}], returning partial result: {}",
          subdir, result.error().ToString());
      return entries;
    }

    // Merge the entries with the result.
    for (auto&& e : result->contents) {
      auto&& added = entries.emplace_back();
      if (!flare::StartsWith(e.key, subdir)) {
        FLARE_LOG_WARNING_EVERY_SECOND(
            "Unexpected entry [{}] from directory [{}].", e.key, subdir);
        continue;
      }
      added.key = e.key.substr(subdir.size());
      added.size = e.size;
      if (auto time = FromIso8601Timestamp(e.last_modified); !time) {
        FLARE_LOG_WARNING_EVERY_SECOND("Failed to parse timestamp [{}].",
                                       e.last_modified);
        added.timestamp = {};
      } else {
        added.timestamp = *time;
      }
      FLARE_VLOG(10, "Got [{}].", e.key);
    }

    // Prepare for reading the next batch.
    marker = result->next_marker;
    if (entries.size() > kMaxEntries) {
      FLARE_LOG_WARNING_EVERY_SECOND(
          "Too many files (more than {}) in [{}]. Ignoring the rest ones.",
          kMaxEntries, subdir);
      return entries;
    }
    if (!result->is_truncated) {
      FLARE_VLOG(1, "Got {} entries in [{}].", entries.size(), subdir);
      return entries;  // All is here.
    }
  }
}

std::vector<CosCacheEngine::EntryDesc> CosCacheEngine::GetEntries() const {
  std::vector<flare::Future<std::vector<EntryDesc>>> results;
  for (int i = 0; i != kSubDirs; ++i) {
    results.emplace_back(flare::fiber::Async([this, i] {
      return GetEntriesIn(flare::Format("{}/{}/", FLAGS_cos_engine_dir, i));
    }));
  }

  // Merge object list.
  auto waited = flare::fiber::BlockingGet(flare::WhenAll(&results));
  std::vector<EntryDesc> merged;
  for (auto&& e : waited) {
    merged.insert(merged.end(), e.begin(), e.end());
  }

  // See if we should purge some file, and save their names.
  std::sort(merged.begin(), merged.end(),
            [](auto&& x, auto&& y) { return x.timestamp > y.timestamp; });
  std::uint64_t total_size = 0;
  auto iter = merged.begin();
  while (iter != merged.end() && total_size < capacity_) {
    total_size += iter->size;
    ++iter;
  }

  // We've used more than `capacity_` bytes, mark the rest (oldest) for removal.
  if (iter != merged.end()) {
    std::scoped_lock _(lock_);
    for (auto current = iter; current != merged.end(); ++current) {
      pending_removal_.push_back(current->key);
    }
  }
  return merged;
}

FLARE_REGISTER_CLASS_DEPENDENCY(cache_engine_registry, "cos", CosCacheEngine);

}  // namespace yadcc::cache
