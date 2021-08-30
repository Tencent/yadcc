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

#include "yadcc/client/common/utility.h"

#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "blake3/blake3.h"

#include "yadcc/client/common/io.h"
#include "yadcc/client/common/logging.h"

using namespace std::literals;

namespace yadcc::client {

namespace {

template <class T>
inline T ReadClock(int type) {
  timespec ts;
  clock_gettime(type, &ts);
  return T((ts.tv_sec * 1'000'000'000LL + ts.tv_nsec) * 1ns);
}

}  // namespace

std::string GetBaseName(const std::string& name) {
  if (auto pos = name.find_last_of('/'); pos != std::string::npos) {
    return name.substr(pos + 1);
  }
  return name;
}

std::string GetPathName(const std::string& name) {
  if (auto pos = name.find_last_of('/'); pos != std::string::npos) {
    return name.substr(0, pos);
  }
  CHECK(0, "Invalid file name [{}].", name);
}

std::string GetCanonicalPath(const std::string& path) {
  char buf[PATH_MAX + 1];
  if (realpath(path.c_str(), buf)) {
    return buf;
  }
  return {};
}

const std::string& GetSelfExecutable() {
  static std::string self = GetCanonicalPath("/proc/self/exe");
  return self;
}

bool StartsWith(const std::string_view& s, const std::string_view& pattern) {
  return s.size() >= pattern.size() && s.substr(0, pattern.size()) == pattern;
}

bool EndsWith(const std::string_view& s, const std::string_view& pattern) {
  return s.size() >= pattern.size() &&
         s.substr(s.size() - pattern.size()) == pattern;
}

std::vector<std::string_view> Split(const std::string_view& text,
                                    const std::string_view& pattern) {
  std::vector<std::string_view> result;
  if (text.empty()) {
    return result;
  }
  CHECK(!pattern.empty());
  auto rest = text;
  while (true) {
    auto pos = rest.find(pattern);
    if (pos == std::string_view::npos) {
      break;
    }
    auto part = rest.substr(0, pos);
    if (!part.empty()) {
      result.push_back(part);
    }
    rest = rest.substr(pos + pattern.size());
  }
  if (!rest.empty()) {
    result.push_back(rest);
  }
  return result;
}

std::string Join(const std::vector<std::string>& parts,
                 const std::string_view& delim) {
  std::string result;
  for (auto&& e : parts) {
    result += e;
    result += delim;
  }
  if (!result.empty()) {
    result.erase(result.end() - delim.size(), result.end());
  }
  return result;
}

std::string FindExecutableInPath(const std::string& executable) {
  return FindExecutableInPath(executable, [](auto&&) { return true; });
}

std::string FindExecutableInPath(
    const std::string& executable,
    const std::function<bool(const std::string&)>& pred) {
  auto path = getenv("PATH");
  while (true) {
    auto eptr = strchr(path, ':');
    int length;
    if (!eptr) {
      length = strlen(path);
    } else {
      length = eptr - path;
    }

    auto dir = std::string_view(path, length);
    LOG_DEBUG("Looking up for [{}] in [{}].", executable, dir);

    auto file = fmt::format("{}/{}", dir, executable);
    auto canonical = GetCanonicalPath(file);
    struct stat buf;
    if (lstat(file.c_str(), &buf) == 0 && canonical != GetSelfExecutable() &&
        pred(canonical)) {
      LOG_TRACE("Found [{}] at [{}].", executable, dir);
      return file;
    }
    if (eptr) {
      path = eptr + 1;
    } else {
      break;
    }
  }
  LOG_FATAL("Failed to find executable [{}] in path.", executable);
}

std::chrono::steady_clock::time_point ReadCoarseSteadyClock() {
  return ReadClock<std::chrono::steady_clock::time_point>(
      CLOCK_MONOTONIC_COARSE);
}

std::pair<std::uint64_t, std::uint64_t> GetMtimeAndSize(
    const std::string& file) {
  struct stat result;
  PCHECK(lstat(file.c_str(), &result) == 0);
  return std::pair(result.st_mtime, result.st_size);
}

std::string Blake3Digest(const std::string& file) {
  auto data = ReadAll(file);
  blake3_hasher state;
  blake3_hasher_init(&state);
  blake3_hasher_update(&state, data.data(), data.size());
  uint8_t output[BLAKE3_OUT_LEN];
  blake3_hasher_finalize(&state, output, BLAKE3_OUT_LEN);
  char hex[BLAKE3_OUT_LEN * 2 + 1] = {0};
  for (int i = 0; i != BLAKE3_OUT_LEN; ++i) {
    snprintf(hex + i * 2, 2 + 1, "%02x", output[i]);
  }
  return std::string(hex, hex + BLAKE3_OUT_LEN * 2);
}

}  // namespace yadcc::client
