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

#ifndef YADCC_DAEMON_CLOUD_TEMPORARY_DIR_H_
#define YADCC_DAEMON_CLOUD_TEMPORARY_DIR_H_

#include <string>
#include <utility>
#include <vector>

#include "flare/base/buffer.h"

namespace yadcc::daemon::cloud {

// Used to get all compilation result files.
class TemporaryDir {
 public:
  TemporaryDir() : is_alive_(false) {}
  explicit TemporaryDir(const std::string& prefix);  // `/absolute-path/to/...`

  ~TemporaryDir();

  TemporaryDir(TemporaryDir&& bundle) noexcept;
  TemporaryDir& operator=(TemporaryDir&& bundle) noexcept;

  // Path to this temporary directory.
  std::string GetPath() const;

  // Read every files generated in our directory.
  //
  // Returns: {(relative_dir, data), ...}
  std::vector<std::pair<std::string, flare::NoncontiguousBuffer>> ReadAll(
      const std::string& subdir = "");

  // Clean up our temporary directory.
  void Dispose();

 private:
  bool is_alive_;
  std::string dir_;
};

}  // namespace yadcc::daemon::cloud

#endif  //  YADCC_DAEMON_CLOUD_TEMPORARY_DIR_H_
