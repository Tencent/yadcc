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

#include "yadcc/daemon/cloud/execute_command.h"

#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

#include "flare/base/logging.h"

#include "yadcc/common/io.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

pid_t StartProgram(const std::string& cmdline, int nice_level, int stdin_fd,
                   int stdout_fd, int stderr_fd, bool in_group) {
  FLARE_VLOG(10, "Executing command: [{}]", cmdline);

  // Unfortunately we can't use `vfork` here as we have something to do before
  // `exec`-ing a new program.
  int pid = fork();
  FLARE_PCHECK(pid >= 0, "Failed to create child process.");
  if (pid == 0) {  // In child process.
    dup2(stdin_fd, STDIN_FILENO);
    dup2(stdout_fd, STDOUT_FILENO);
    dup2(stderr_fd, STDERR_FILENO);
    lseek(STDIN_FILENO, 0, SEEK_SET);  // Rewind file position.
    close(stdin_fd);
    close(stdout_fd);
    close(stderr_fd);

    // Close all possible fds in children. TODO(luobogao): This is ugly.
    for (int i = 3; i != 9999; ++i) {
      (void)close(i);
    }

    if (nice_level) {
      nice(nice_level);
    }
    if (in_group) {
      setpgid(0, 0);
    }

    // Reset CWD.
    //
    // Otherwise if we're removed from disk without being killed, `sh` would
    // print a warning on start up.
    FLARE_PCHECK(chdir("/") == 0);

    // Onion implements its own hooked version of `execl` in a rather buggy way,
    // by allocating memory in its hook. That may hang the program unexpectedly
    // when run with heap profiler (e.g., when running as UT).
    //
    // Therefore, to get ride of their buggy implementation, we do `syscall`
    // directly.
    //
    // TODO(luobogao): We might want to split `cmdline` to avoid `E2BIG` here.
    const char* const argvs[] = {"sh", "-c", cmdline.c_str(), nullptr};
    syscall(SYS_execve, "/bin/sh", argvs, environ);
    _exit(127);
  }

  return pid;
}

}  // namespace yadcc::daemon::cloud
