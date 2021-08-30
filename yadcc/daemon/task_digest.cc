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

#include "yadcc/daemon/task_digest.h"

#include <string>
#include <vector>

#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding/hex.h"

namespace yadcc::daemon {

std::string GetCxxTaskDigest(const EnvironmentDesc& env_desc,
                             const std::string_view& invocation_arguments,
                             const std::string_view& source_digest) {
  return flare::EncodeHex(flare::Blake3({"cxx2", env_desc.compiler_digest(),
                                         invocation_arguments, source_digest}));
}

}  // namespace yadcc::daemon
