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

#include "yadcc/client/cxx/libfakeroot.h"

#include <fcntl.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fmt/format.h"

#include "yadcc/client/common/io.h"
#include "yadcc/client/common/logging.h"
#include "yadcc/client/cxx/libfakeroot/libfakeroot.h"

namespace yadcc::client {

namespace {

const std::string_view kLibFakeRootPayload(
    reinterpret_cast<const char*>(yadcc_client_cxx_libfakeroot_libfakeroot_so),
    yadcc_client_cxx_libfakeroot_libfakeroot_so_len);

// Well, it actually user's home directory.
const std::string kWayToHome = [] {
  // Shamelessly copied from https://stackoverflow.com/a/26696759.
  auto ptr = getenv("HOME");
  if (!ptr) {
    // NOT thread-safe. (We don't need thread-safety anyway.).
    ptr = getpwuid(getuid())->pw_dir;
  }
  return ptr;
}();

// Prevent race in extracting `libfakeroot.so` between multiple yadcc instances.
std::string GetLockPath() {
  static const std::string kPath = [] {
    auto&& home_dir = kWayToHome;
    (void)mkdir(fmt::format("{}/.yadcc", home_dir).c_str(), 0755);
    (void)mkdir(fmt::format("{}/.yadcc/lock", home_dir).c_str(), 0755);
    return fmt::format("{}/.yadcc/lock/libfakeroot.lock", home_dir);
  }();
  return kPath;
}

int LockForExtraction() {
  auto path = GetLockPath();
  auto fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  PCHECK(fd != -1, "Failed to open [{}].", path);
  PCHECK(flock(fd, LOCK_EX) == 0, "Failed to lock [{}].", path);
  return fd;
}

void UnlockForExtraction(int handle) {
  PCHECK(flock(handle, LOCK_UN) == 0, "Failed to unlock.");
  PCHECK(close(handle) == 0);
}

}  // namespace

// Exposed (i.e., not put into anonymous namespace) for testing purpose. Not a
// public API.
void ExtractLibFakeRootTo(const std::string& path) {
  auto lock = LockForExtraction();
  LOG_TRACE(
      "Cannot find `libfakeroot.so` or the existing one it out-of-date, "
      "extracting it.");

  // We've grabbed the lock via `LockForExtraction()`, so we should be the
  // sole writer.
  auto temp_dir = fmt::format("{}.writing", path);

  // Failure leads to crash.
  WriteAll(temp_dir, kLibFakeRootPayload);
  // `rename` guarantees us atomicity. If anyone else is using (or going to
  // use) the `libfakeroot.so`, he/she should either see the old one, or the
  // new one, but not a corrupted one.
  PCHECK(rename(temp_dir.c_str(), path.c_str()) == 0,
         "Failed to extract `libfakeroot.so` to [{}].", path);
  UnlockForExtraction(lock);

  // We can actually fail back to not using compilation cache instead of raising
  // a hard error.
  struct stat buf;
  CHECK(lstat(path.c_str(), &buf) == 0 &&
            buf.st_size == kLibFakeRootPayload.size(),
        "Failed to extract `libfakeroot.so`.");
}

std::string GetLibFakeRootPath() {
  static auto kPath = fmt::format("{}/.yadcc/lib/libfakeroot.so", kWayToHome);
  LOG_TRACE("Looking for `libfakeroot.so` at [{}].", kPath);

  struct stat buf;
  if (lstat(kPath.c_str(), &buf) || buf.st_size != kLibFakeRootPayload.size()) {
    auto&& home_dir = kWayToHome;
    (void)mkdir(fmt::format("{}/.yadcc", home_dir).c_str(), 0755);
    (void)mkdir(fmt::format("{}/.yadcc/lib", home_dir).c_str(), 0755);
    // If there's already one (we're upgrading it then), leave it alone. We
    // don't want to `unlink` it so as not to disturb others. Instead, we
    // extract to a temp file and atomically rename our temp file to the desired
    // location.

    // Now extract `libfakeroot.so`.
    ExtractLibFakeRootTo(kPath);
  } else {
    LOG_TRACE("Using existing `libfakeroot.so`.");
  }
  return kPath;
}

}  // namespace yadcc::client
