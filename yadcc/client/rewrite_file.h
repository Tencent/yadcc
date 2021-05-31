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

#ifndef YADCC_CLIENT_REWRITE_FILE_H_
#define YADCC_CLIENT_REWRITE_FILE_H_

#include <optional>
#include <string>

#include "yadcc/client/compiler_args.h"
#include "yadcc/client/env_options.h"

namespace yadcc::client {

struct RewriteResult {
  // Source code language.
  std::string language;

  // Path of source code. Primarily for debugging purpose.
  std::string source_path;

  // Set if source code was successfully rewritten with `-fdirectives-only`.
  bool directives_only;

  // Determines if compilation cache is allowed.
  CacheControl cache_control;

  // Preprocessed source, zstd compressed.
  std::string zstd_rewritten;

  // BLAKE3 digest of the (uncompressed) preprocessed source.
  std::string source_digest;
};

// Rewrite source file to make it self-contained.
std::optional<RewriteResult> RewriteFile(const CompilerArgs& args);

}  // namespace yadcc::client

#endif  // YADCC_CLIENT_REWRITE_FILE_H_
