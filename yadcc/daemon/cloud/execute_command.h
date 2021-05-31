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

#ifndef YADCC_DAEMON_CLOUD_EXECUTE_COMMAND_H_
#define YADCC_DAEMON_CLOUD_EXECUTE_COMMAND_H_

#include <string>

#include "flare/base/buffer.h"

namespace yadcc::daemon::cloud {

// Start a new program.
//
// `stdin` / `stdout` / `stderr` is redirected to `stdin_fd` / `stdout_fd` /
// stderr_fd` respectively.
//
// If `in_group` is set, the program is started in its own process group. You
// can kill the entire process group (i.e., the program and all its child) by
// `kill(-pid_returned, ...)`.
//
// Internally we use `/bin/sh sh -c` to run `cmdline`. This incurs a perf.
// penalty (~3ms overhead on my machine). If possible we should call `exec` with
// program & arguments directly.
pid_t StartProgram(const std::string& cmdline, int nice_level, int stdin_fd,
                   int stdout_fd, int stderr_fd, bool in_group);

}  // namespace yadcc::daemon::cloud

#endif  //  YADCC_DAEMON_CLOUD_EXECUTE_COMMAND_H_
