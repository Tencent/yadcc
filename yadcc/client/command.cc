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

#include "yadcc/client/command.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __USE_GNU
#define __USE_GNU
#endif

#include <fcntl.h>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yadcc/client/io.h"
#include "yadcc/client/logging.h"

using namespace std::literals;

namespace yadcc::client {

namespace {

struct ProgramStartupInfo {
  int pid;
  int stdin_writer, stdout_reader, stderr_reader;
};

std::string RebuildCommandForLogging(const std::string& command,
                                     const char** argv) {
  std::string result = command;
  while (*argv) {
    result += " "s + *argv++;
  }
  return result;
}

// [read fd, write fd].
std::pair<int, int> CreatePipe() {
  int fds[2];
  PCHECK(pipe2(fds, 0) == 0);
  PCHECK(fcntl(fds[1], F_SETPIPE_SZ, 131072) > 0 || errno == EPERM);
  return std::pair(fds[0], fds[1]);
}

std::vector<const char*> BuildArguments(const RewrittenArgs& command) {
  std::vector<const char*> result;
  result.push_back(command.GetProgram().c_str());
  for (auto&& e : command.Get()) {
    result.push_back(e.c_str());
  }
  result.push_back(nullptr);
  return result;
}

ProgramStartupInfo StartProgram(
    const RewrittenArgs& command,
    const std::initializer_list<std::string>& extra_envs) {
  int stdin_reader, stdout_writer, stderr_writer;  // Used by the child.
  int stdin_writer, stdout_reader, stderr_reader;  // By the parent.

  std::tie(stdin_reader, stdin_writer) = CreatePipe();
  std::tie(stdout_reader, stdout_writer) = CreatePipe();
  std::tie(stderr_reader, stderr_writer) = CreatePipe();

  std::vector<char*> envs_storage;
  char** envs = environ;
  if (extra_envs.size()) {
    auto ptr = environ;
    while (*ptr) {
      envs_storage.push_back(*ptr++);
    }
    for (auto&& e : extra_envs) {
      envs_storage.push_back(const_cast<char*>(e.c_str()));
    }
    envs_storage.push_back(0);
    envs = envs_storage.data();
  }
  auto argvs = BuildArguments(command);

  // `vfork` is faster, in theory. However, use it with caution as it borrows
  // parent's address space.
  int pid = vfork();
  PCHECK(pid >= 0, "Failed to create child process.");
  if (pid == 0) {  // In child process.
    dup2(stdin_reader, STDIN_FILENO);
    dup2(stdout_writer, STDOUT_FILENO);
    dup2(stderr_writer, STDERR_FILENO);
    close(stdin_reader);
    close(stdout_writer);
    close(stderr_writer);
    close(stdin_writer);
    close(stdout_reader);
    close(stderr_reader);

    // FIXME: Do we have to close all fds in children?
    //
    // FIXME: How can we handle failure here?
    syscall(SYS_execve, command.GetProgram().c_str(), argvs.data(), envs);
    _exit(127);
  }

  close(stdin_reader);
  close(stdout_writer);
  close(stderr_writer);

  SetNonblocking(stdin_writer);
  SetNonblocking(stdout_reader);
  SetNonblocking(stderr_reader);

  return ProgramStartupInfo{pid, stdin_writer, stdout_reader, stderr_reader};
}

void HandleProgramIoAndCloseHandles(int fdin, int fdout, int fderr,
                                    const std::string& input,
                                    OutputStream* standard_output,
                                    std::string* standard_error) {
  thread_local char io_buffer[131072];

  std::size_t stdin_bytes = 0, stdout_bytes = 0;
  bool in_done = false, out_done = false, err_done = false;

  if (input.empty()) {
    PCHECK(close(fdin) == 0);
    in_done = true;
  }

  while (!in_done || !out_done || !err_done) {
    pollfd fds[3] = {};
    int nfds = 0;

    // Let's see which handles are still not finished.
    if (!in_done) {
      fds[nfds++] = pollfd{.fd = fdin, .events = POLLOUT};
    }
    if (!out_done) {
      fds[nfds++] = pollfd{.fd = fdout, .events = POLLIN};
    }
    if (!err_done) {
      fds[nfds++] = pollfd{.fd = fderr, .events = POLLIN};
    }
    auto events = poll(fds, nfds, -1);
    if (events < 0) {
      if (errno == EINTR) {
        continue;
      }
      PCHECK(0, "Failed to poll on child pipe.");
    }

    // Handle the events.
    for (int i = 0; i != nfds; ++i) {
      if (fds[i].revents == 0) {
        continue;
      }
      if (fds[i].fd == fdin) {
        auto bytes = WriteTo(fdin, input, stdin_bytes);
        if (bytes >= 0) {
          stdin_bytes += bytes;
          if (stdin_bytes == input.size()) {
            in_done = true;
            PCHECK(close(fdin) == 0);
          }
        } else {
          LOG_WARN("Child process unexpectedly closed stdin.");
          in_done = true;  // So be it.
          PCHECK(close(fdin) == 0);
        }
      } else if (fds[i].fd == fdout) {
        auto bytes = ReadBytes(fdout, io_buffer, sizeof(io_buffer));
        if (bytes > 0) {
          standard_output->Write(io_buffer, bytes);
          stdout_bytes += bytes;
        } else {
          PCHECK(bytes == 0);  // `EAGAIN` (if occurs) is not expected.
          out_done = true;
          PCHECK(close(fdout) == 0);
        }
      } else if (fds[i].fd == fderr) {
        auto bytes = ReadBytes(fderr, io_buffer, sizeof(io_buffer));
        if (bytes > 0) {
          // Bytes are copied here. I don't think there will be too much output
          // through stderr, so don't worry.
          standard_error->append(io_buffer, bytes);
        } else {
          PCHECK(bytes == 0);  // Don't `EAGAIN`.
          err_done = true;
          PCHECK(close(fderr) == 0);
        }
      }
    }
  }
  LOG_DEBUG(
      "Wrote [{}] bytes to stdin, read [{}] bytes from stdout, [{}] bytes from "
      "stderr.",
      stdin_bytes, stdout_bytes, standard_error->size());
}

int GetProgramExitCode(int pid) {
  int status;
  while (true) {
    auto result = waitpid(pid, &status, 0);
    if (result == -1) {
      if (errno == EINTR) {
        continue;
      }
      PCHECK(0, "Failed to wait on child process.");
    }
    break;
  }
  CHECK(WIFEXITED(status),
        "Child process exited unexpectedly with status [{}].", status);
  return WEXITSTATUS(status);
}

}  // namespace

ExecutionResult ExecuteCommand(const RewrittenArgs& command,
                               const std::string& input) {
  TransparentOutputStream os;
  std::string error;
  auto&& [pid, fdin, fdout, fderr] = StartProgram(command, {});
  HandleProgramIoAndCloseHandles(fdin, fdout, fderr, input, &os, &error);
  auto ec = GetProgramExitCode(pid);
  LOG_DEBUG("Command completed with status [{}].", ec);
  return ExecutionResult{.exit_code = ec, .output = os.Get(), .error = error};
}

int ExecuteCommand(const RewrittenArgs& command,
                   const std::initializer_list<std::string>& extra_envs,
                   const std::string& input, OutputStream* standard_output,
                   std::string* standard_error) {
  LOG_DEBUG("Executing command: [{}]", command.ToCommandLine(true));
  auto&& [pid, fdin, fdout, fderr] = StartProgram(command, extra_envs);
  HandleProgramIoAndCloseHandles(fdin, fdout, fderr, input, standard_output,
                                 standard_error);
  auto ec = GetProgramExitCode(pid);
  LOG_DEBUG("Command completed with status [{}].", ec);
  return ec;
}

int PassthroughToProgram(const std::string& program, const char** argv) {
  LOG_DEBUG("Passing though to [{}].", RebuildCommandForLogging(program, argv));

  // Rebuild `argv`s.
  std::vector<const char*> argvs;
  argvs.push_back(program.c_str());
  while (*argv) {
    argvs.push_back(*argv++);
  }
  argvs.push_back(nullptr);

  // Passthrough to `program`.
  int pid = fork();
  PCHECK(pid >= 0, "Failed to create child process.");
  if (pid == 0) {  // In child process.
    // FIXME: How to handle failure here? There's few we can safely do after
    // `fork()`.
    execvp(program.c_str(), const_cast<char* const*>(argvs.data()));  // ...
    _exit(127);
  }
  return GetProgramExitCode(pid);
}

}  // namespace yadcc::client
