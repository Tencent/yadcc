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

#ifndef YADCC_CLIENT_COMMON_REWRITTEN_ARGS_H_
#define YADCC_CLIENT_COMMON_REWRITTEN_ARGS_H_

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yadcc/client/common/span.h"

namespace yadcc::client {

// Describes a (possibly rewritten) `CompilerArgs`.
class RewrittenArgs {
 public:
  explicit RewrittenArgs(std::string program, std::vector<std::string> args)
      : program_(std::move(program)), args_(std::move(args)) {}

  // Get path to the program.
  const std::string& GetProgram() const noexcept { return program_; }

  // Get arguments (not include the program itself.).
  const std::vector<std::string>& Get() const noexcept { return args_; }

  // Concatenate the arguments after escaping them. The resulting string can be
  // passed to shell to run them.
  std::string ToCommandLine(bool with_program) const;

  // Concatenate arguments without escaping. This can be more friendly for
  // logging purpose.
  std::string ToString() const;

 private:
  std::string program_;
  std::vector<std::string> args_;
};

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMMON_REWRITTEN_ARGS_H_
