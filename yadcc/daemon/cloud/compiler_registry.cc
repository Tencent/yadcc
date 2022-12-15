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

#include "yadcc/daemon/cloud/compiler_registry.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "gflags/gflags.h"

#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding.h"
#include "flare/base/logging.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"

#include "yadcc/common/dir.h"

using namespace std::literals;

DEFINE_int32(compiler_rescan_interval, 60,
             "Compiler rescan interval, in seconds");
DEFINE_string(
    extra_compiler_dirs, "",
    "By default, yadcc recognizes compilers appears in PATH. To add compilers "
    "that does not appear in PATH, list them here. Paths should be separated "
    "by a colon (':'). e.g. `/usr/local/tools/gcc/bin`");
DEFINE_string(
    extra_compiler_bundle_dirs, "",
    "If you have multiple compilers installed inside the same parent "
    "directory, you may simply specify the parent directory here. We enumerate "
    "every compiler in `<bundle_dir1>/*/bin`, `<bundle_dir2>/*/bin`, .... "
    "Paths should be separated by a colon.");

namespace yadcc::daemon::cloud {

namespace {

std::optional<std::string> TryGetFileDigest(const std::string_view& path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    FLARE_LOG_ERROR("Failed to open [{}].", path);
    return std::nullopt;
  }
  return flare::EncodeHex(
      flare::Blake3(std::string(std::istreambuf_iterator<char>(input), {})));
}

std::string PathJoin(const std::string_view& x, const std::string_view& y) {
  if (x.empty() || x.back() == '/') {
    return flare::Format("{}{}", x, y);
  }
  return flare::Format("{}/{}", x, y);
}

// Test if `path` is executable by us, and if so, returns its canonical path.
std::optional<std::string> GetCanonicalPathIfExecutable(
    const std::string& path) {
  struct stat st;
  if (lstat(path.c_str(), &st) == -1) {  // 404 Not Found.
    return std::nullopt;
  }

  char buf[PATH_MAX + 1];
  if (!realpath(path.c_str(), buf)) {
    return std::nullopt;
  }

  auto executable = (geteuid() == st.st_uid && (st.st_mode & S_IXUSR)) ||
                    (getegid() == st.st_gid && (st.st_mode & S_IXGRP)) ||
                    (st.st_mode & S_IXOTH);
  if (!executable) {
    return std::nullopt;
  }
  return buf;
}

// Compiler wrappers provided by ccache / distcc / icecc. They're not treated as
// compiler by us.
bool IsCompilerWrapper(const std::string& path) {
  static const std::vector<std::string> kWrappers = {"ccache", "distcc",
                                                     "icecc"};
  for (auto&& e : kWrappers) {
    if (flare::EndsWith(path, e)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> TryLookupCompilerIn(const std::string_view& dir) {
  static constexpr auto kCompilerExecutables = {
      "gcc", "g++",
      // I'm not sure if we support Clang, TBH.
      "clang", "clang++"};
  std::vector<std::string> result;

  for (auto&& e : kCompilerExecutables) {
    auto path = GetCanonicalPathIfExecutable(PathJoin(dir, e));

    if (!path || IsCompilerWrapper(*path)) {
      continue;
    }
    result.push_back(*path);
  }
  return result;
}

std::vector<std::string_view> GetDirectoriesInPath() {
  return flare::Split(getenv("PATH"), ":");
}

// Register a compilation environment.
void AddEnvironmentTo(const std::string_view& path,
                      std::unordered_map<std::string, std::string>* temp_paths,
                      std::vector<EnvironmentDesc>* temp_envs) {
  EnvironmentDesc desc;
  auto digest = TryGetFileDigest(path);
  if (digest == std::nullopt) {
    return;
  }
  desc.set_compiler_digest(*digest);

  auto env_string = desc.compiler_digest();
  if (temp_paths->count(env_string) == 0) {
    (*temp_paths)[env_string] = path;
    temp_envs->push_back(desc);
  }  // Duplicates are ignored silently.
}

}  // namespace

CompilerRegistry* CompilerRegistry::Instance() {
  static flare::NeverDestroyed<CompilerRegistry> instance;
  return instance.Get();
}

CompilerRegistry::CompilerRegistry() {
  // Set up a timer to periodically recheck compilers present.
  compiler_scanner_timer_ = flare::fiber::SetTimer(
      FLAGS_compiler_rescan_interval * 1s, [this] { OnCompilerRescanTimer(); });
  OnCompilerRescanTimer();
}

CompilerRegistry::~CompilerRegistry() {}

std::vector<EnvironmentDesc> CompilerRegistry::EnumerateEnvironments() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return environments_;
}

std::optional<std::string> CompilerRegistry::TryGetCompilerPath(
    const EnvironmentDesc& env) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto iter = compiler_paths_.find(env.compiler_digest());
  if (iter != compiler_paths_.end()) {
    return iter->second;
  }
  return {};
}

void CompilerRegistry::Stop() {
  flare::fiber::KillTimer(compiler_scanner_timer_);
}

void CompilerRegistry::Join() {}

void CompilerRegistry::OnCompilerRescanTimer() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  std::unordered_map<std::string, std::string> temp_paths;
  std::vector<EnvironmentDesc> temp_envs;

  // Compilers in `PATH`.
  for (auto&& dir : GetDirectoriesInPath()) {
    for (auto&& e : TryLookupCompilerIn(dir)) {
      AddEnvironmentTo(e, &temp_paths, &temp_envs);
    }
  }

  // User-supplied compilers.
  for (auto&& dir : flare::Split(FLAGS_extra_compiler_dirs, ":")) {
    for (auto&& e : TryLookupCompilerIn(dir)) {
      AddEnvironmentTo(e, &temp_paths, &temp_envs);
    }
  }

  // User-supplied compiler bundles..
  for (auto&& dir : flare::Split(FLAGS_extra_compiler_bundle_dirs, ":")) {
    struct stat buf;
    if (lstat(flare::Format("{}", dir).c_str(), &buf) == 0 &&
        S_ISDIR(buf.st_mode)) {
      std::vector<DirEntry> subdirs = EnumerateDir(flare::Format("{}", dir));
      for (auto&& subdir : subdirs) {
        for (auto&& e : TryLookupCompilerIn(
                 flare::Format("{}/{}/bin", dir, subdir.name))) {
          AddEnvironmentTo(e, &temp_paths, &temp_envs);
        }
      }
    }
  }

  // RHEL-specific.
  for (int i = 1; i != 100; ++i) {
    for (auto&& e : TryLookupCompilerIn(
             flare::Format("/opt/rh/devtoolset-{}/root/bin", i))) {
      AddEnvironmentTo(e, &temp_paths, &temp_envs);
    }
  }

  UpdateEnvironment(temp_paths, temp_envs);
}

void CompilerRegistry::UpdateEnvironment(
    const std::unordered_map<std::string, std::string>& temp_paths,
    const std::vector<EnvironmentDesc>& temp_envs) {
  // Compilers that are gone or newly-founded are printed after sorted. This is
  // done for better diagnostics readability.
  std::vector<std::string> gone, found;

  for (auto iter = compiler_paths_.begin(); iter != compiler_paths_.end();
       ++iter) {
    if (temp_paths.count(iter->first) == 0) {
      gone.emplace_back(iter->second);
    }
  }
  for (auto iter = temp_paths.begin(); iter != temp_paths.end(); ++iter) {
    if (compiler_paths_.count(iter->first) == 0) {
      found.emplace_back(iter->second);
    }
  }

  std::sort(gone.begin(), gone.end());
  std::sort(found.begin(), found.end());
  for (auto&& e : gone) {
    FLARE_LOG_INFO("Compiler [{}] has gone, forgetting about it.", e);
  }
  for (auto&& e : found) {
    FLARE_LOG_INFO("Found compiler: {}", e);
  }

  compiler_paths_ = temp_paths;
  environments_ = temp_envs;
}

}  // namespace yadcc::daemon::cloud
