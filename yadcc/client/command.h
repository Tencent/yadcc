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

#ifndef YADCC_CLIENT_COMMAND_H_
#define YADCC_CLIENT_COMMAND_H_

#include <initializer_list>
#include <string>

#include "yadcc/client/compiler_args.h"
#include "yadcc/client/output_stream.h"

namespace yadcc::client {

struct ExecutionResult {
  int exit_code;
  std::string output;  // stdout
  std::string error;   // stderr
};

ExecutionResult ExecuteCommand(const RewrittenArgs& command,
                               const std::string& input = "");

int ExecuteCommand(const RewrittenArgs& command,
                   const std::initializer_list<std::string>& extra_envs,
                   const std::string& input, OutputStream* standard_output,
                   std::string* standard_error);

// Execute `program` with the given arguments, passing-through everything from
// `stdin` to it, and everything from its `stdout` / `stderr` to ours.
//
// `argv` should be `nullptr`-terminated (already guaranteed by C standard if
// you got it from `main`.).
//
// Returns exit code of the execution.
int PassthroughToProgram(const std::string& program, const char** argv);

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMMAND_H_
