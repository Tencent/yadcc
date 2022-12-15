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

#ifndef YADCC_DAEMON_CLOUD_COMPILER_REGISTRY_H_
#define YADCC_DAEMON_CLOUD_COMPILER_REGISTRY_H_

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gtest/gtest_prod.h"

#include "yadcc/api/env_desc.pb.h"

namespace yadcc::daemon::cloud {

// To avoid using a different version of compiler to compile user's code, we
// support hosting multiple versions of compilers at the same machine.
//
// This class provides you a way to find the compiler we should invoke, by
// compiler's hash.
class CompilerRegistry {
 public:
  static CompilerRegistry* Instance();

  CompilerRegistry();
  ~CompilerRegistry();

  // Get all known compilation environment.
  std::vector<EnvironmentDesc> EnumerateEnvironments() const;

  // Get compiler path matching the requested environment.
  std::optional<std::string> TryGetCompilerPath(
      const EnvironmentDesc& env) const;

  void Stop();
  void Join();

 private:
  FRIEND_TEST(CompilerRegistry, All);
  FRIEND_TEST(CompilerRegistry, Delete);
  FRIEND_TEST(CompilerRegistry, Stability);

  void OnCompilerRescanTimer();

  void UpdateEnvironment(
      const std::unordered_map<std::string, std::string>& temp_paths,
      const std::vector<EnvironmentDesc>& temp_envs);

 private:
  mutable std::shared_mutex mutex_;

  std::unordered_map<std::string, std::string> compiler_paths_;

  // Registered compilation environments.
  std::vector<EnvironmentDesc> environments_;

  std::uint64_t compiler_scanner_timer_;
};

}  // namespace yadcc::daemon::cloud

#endif  //  YADCC_DAEMON_CLOUD_COMPILER_REGISTRY_H_
