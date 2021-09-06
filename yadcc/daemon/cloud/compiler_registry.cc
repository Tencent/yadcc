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

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "thirdparty/gflags/gflags.h"

#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding.h"
#include "flare/base/logging.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/string.h"

DEFINE_string(
    extra_compiler_dirs, "",
    "By default, yadcc recognizes compilers appears in PATH. To add compilers "
    "that does not appear in PATH, list them here. Paths should be separated "
    "by a colon (':'). e.g. `/usr/local/tools/gcc/bin`");

namespace yadcc::daemon::cloud {

namespace {

std::string GetFileDigest(const std::string_view& path) {
  std::ifstream input(std::string(path), std::ios::binary);
  FLARE_CHECK(input, "Failed to open [{}].", path);
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

}  // namespace

CompilerRegistry* CompilerRegistry::Instance() {
  static flare::NeverDestroyed<CompilerRegistry> instance;
  return instance.Get();
}

CompilerRegistry::CompilerRegistry() {
  // User-supplied compilers.
  for (auto&& dir : flare::Split(FLAGS_extra_compiler_dirs, ":")) {
    for (auto&& e : TryLookupCompilerIn(dir)) {
      RegisterEnvironment(e);
    }
  }

  // Compilers in `PATH`.
  for (auto&& dir : GetDirectoriesInPath()) {
    for (auto&& e : TryLookupCompilerIn(dir)) {
      RegisterEnvironment(e);
    }
  }

  // RHEL-specific.
  for (int i = 1; i != 100; ++i) {
    for (auto&& e : TryLookupCompilerIn(
             flare::Format("/opt/rh/devtoolset-{}/root/bin", i))) {
      RegisterEnvironment(e);
    }
  }

  // TODO(luobogao): Set up a timer to periodically recheck compilers present.
}

CompilerRegistry::~CompilerRegistry() {}

std::vector<EnvironmentDesc> CompilerRegistry::EnumerateEnvironments() const {
  return environments_;
}

std::optional<std::string> CompilerRegistry::TryGetCompilerPath(
    const EnvironmentDesc& env) const {
  auto iter = compiler_paths_.find(GetEnvironmentString(env));
  if (iter != compiler_paths_.end()) {
    return iter->second;
  }
  return {};
}

std::string CompilerRegistry::GetEnvironmentString(
    const EnvironmentDesc& desc) {
  return desc.compiler_digest();
}

void CompilerRegistry::RegisterEnvironment(const std::string_view& path) {
  EnvironmentDesc desc;
  desc.set_compiler_digest(GetFileDigest(path));

  if (compiler_paths_.count(GetEnvironmentString(desc)) == 0) {
    FLARE_LOG_INFO("Found compiler: {}", path);
    compiler_paths_[GetEnvironmentString(desc)] = path;
    environments_.push_back(desc);
  }  // Duplicates are ignored silently.
}

}  // namespace yadcc::daemon::cloud
