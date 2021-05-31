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

#ifndef YADCC_CLIENT_COMPILATION_SAAS_H_
#define YADCC_CLIENT_COMPILATION_SAAS_H_

#include <optional>
#include <string>

#include "yadcc/client/compiler_args.h"
#include "yadcc/client/rewrite_file.h"

// Keeping introducing new terms such as "SAAS" / "cloud" / "private cloud" /
// "public cloud" to describe old things is just, IMO, silly.

namespace yadcc::client {

struct CompilationResult {
  int exit_code;
  std::string output, error;
  std::string bytes;
};

// Submit compilation task to the cloud.
CompilationResult CompileOnCloud(const CompilerArgs& args,
                                 RewriteResult rewritten_source);

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMPILATION_SAAS_H_
