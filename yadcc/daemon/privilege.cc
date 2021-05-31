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

#include "yadcc/daemon/privilege.h"

#include <grp.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include "flare/base/logging.h"

// Shamelessly copied from distcc.
//
// @sa: https://github.com/distcc/distcc/blob/master/src/setuid.c

namespace yadcc::daemon {

// Tests if we're running as root.
bool IsRunningAsRoot() { return getuid() == 0 || geteuid() == 0; }

// Get preferred UID / GID we should use for our daemon.
//
// This is only applicable if we're running as root.
std::pair<uid_t, gid_t> GetPreferredUser() {
  struct passwd pwd;
  // TODO(luobogao): Determine required buffer size via `_SC_GETPW_R_SIZE_MAX`.
  char buf[16384];  // This should be large enough.

  for (auto&& name : {"yadcc", "daemon", "nobody"}) {
    struct passwd* pw;

    // `getpwnam` is indeed easier to use (and we don't care about thread-safety
    // here). Yet it crashes on some systems, due to large allocation in
    // `_nss_ldap_getpwnam_r` (128KiB). That allocation causes stack-overflow in
    // our fiber environment.
    getpwnam_r(name, &pwd, buf, sizeof(buf), &pw);
    if (pw != nullptr) {
      return {pw->pw_uid, pw->pw_gid};
    }
  }

  FLARE_LOG_WARNING(
      "Failed to determine non-privileged UID / GID, failing back to 65534.");
  return {65534, 65534};
}

void DropPrivileges() {
  // TODO(luobogao): So long as we have `CAP_SETGID` privilege we should always
  // switch to a non-privileged user, even if we're not running as root.
  if (!IsRunningAsRoot()) {
    FLARE_LOG_INFO("Not running as root, no privilege to drop.");
    return;
  }

  auto [uid, gid] = GetPreferredUser();
  FLARE_PCHECK(setgid(gid) == 0);
  FLARE_PCHECK(setgroups(1, &gid) == 0);  // Drop supplementary groups.
  FLARE_PCHECK(getgroups(1, &gid) == 1);  // `gid` is modified here.
  FLARE_PCHECK(setuid(uid) == 0);
  // Keep `/proc/self/fd` accessible. FIXME: Any side-effect here?
  FLARE_PCHECK(prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == 0);
  FLARE_PCHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);

  FLARE_LOG_FATAL_IF(getuid() == 0 || geteuid() == 0,
                     "Failed to drop privileges.");

  FLARE_LOG_INFO("Privilege dropped, running as UID {}, GID {}.", getuid(),
                 getgid());
}

}  // namespace yadcc::daemon
