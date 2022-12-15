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

#include "yadcc/daemon/temp_dir.h"

#include "gflags/gflags.h"

#include "flare/base/logging.h"

#include "yadcc/daemon/sysinfo.h"

DEFINE_string(temporary_dir, "/dev/shm",
              "For performance reasons, by default we store temporary files in "
              "`/dev/shm`. If your machine can't afford this, you can specify "
              "a different directory here.");

namespace yadcc::daemon {

// In order for UT to be able to access, this method is not specifically placed
// in the anonymous space.
std::string DetermineTemporaryDirectory() {
  // We use `/dev/shm` (disk) only if its available space is larger than
  // `kMinimumBytesForRamDisk`
  constexpr auto kMinimumBytesForRamDisk = 10ULL * 1024 * 1024 * 1024;

  if (FLAGS_temporary_dir.empty()) {
    return FLAGS_temporary_dir;
  }
  std::size_t available_size = GetDiskAvailableSize("/dev/shm");
  if (available_size < kMinimumBytesForRamDisk) {
    return "/tmp";
  }
  return "/dev/shm";
}

const std::string& GetTemporaryDir() {
  static const auto result = DetermineTemporaryDirectory();
  return result;
}

}  // namespace yadcc::daemon
