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

#ifndef YADCC_CLIENT_COMPILER_ARGS_H_
#define YADCC_CLIENT_COMPILER_ARGS_H_

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yadcc/client/span.h"

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

 private:
  std::string program_;
  std::vector<std::string> args_;
};

// This class helps you to parse and edit compiler's arguments.
class CompilerArgs {
 public:
  using OptionArgs = Span<const char*>;

  // Parse compiler args. It's your responsibility to make sure `argv` is not
  // modified after this class is instantiated.
  CompilerArgs(int argc, const char** argv);

  // Get compiler binary name.
  const char* GetCompiler() const noexcept { return compiler_.c_str(); }

  // Override compiler binary name (or path).
  void SetCompiler(std::string path) noexcept { compiler_ = std::move(path); }

  const std::vector<const char*>& GetFilenames() const noexcept {
    return filenames_;
  }

  // Try to find an option with the given name, and return its option values.
  const OptionArgs* TryGet(const std::string_view& key) const;

  // Find option matching the given prefix.
  const OptionArgs* TryGetByPrefix(const std::string_view& prefix) const;

  // Generate a new argument list. Options matching `remove` exactly, or with
  // prefix in `remove_prefix` are removed, and options in `add` are appended to
  // the result.
  RewrittenArgs Rewrite(const std::unordered_set<std::string_view>& remove,
                        const std::vector<std::string_view>& remove_prefix,
                        const std::initializer_list<std::string_view>& add,
                        bool keep_filenames) const;

  // Recreate a command line equivalent to what's parsed by us. Due to argument
  // escaping in shell, the resuling string might not be exactly the same as the
  // one we're invoked with, but they should be of same meaning.
  std::string Rebuild() const { return compiler_ + " " + rebuilt_; }

 private:
  std::string compiler_;

  // Order is significant, so don't use a map instead.
  std::vector<std::pair<const char*, OptionArgs>> args_;

  // Everything not an option value of the preceding argument and started with a
  // `-` is treated as a filename.
  std::vector<const char*> filenames_;

  // Full command line rebuilt from `argv` passed to our constructor. This is
  // NOT the same as concatenating everything in `compiler_` / `args_` /
  // `filenames_`. For linking object files, filename position relative to other
  // arguments (e.g., `-lssl`) is significant.
  std::string rebuilt_;
};

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMPILER_ARGS_H_
