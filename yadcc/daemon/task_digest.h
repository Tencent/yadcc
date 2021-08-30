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

#ifndef YADCC_DAEMON_TASK_DIGEST_H_
#define YADCC_DAEMON_TASK_DIGEST_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "yadcc/api/env_desc.pb.h"

namespace yadcc::daemon {

// Generates a digest for C++ task.
std::string GetCxxTaskDigest(const EnvironmentDesc& env_desc,
                             const std::string_view& invocation_arguments,
                             const std::string_view& source_digest);

}  // namespace yadcc::daemon

#endif  // YADCC_DAEMON_TASK_DIGEST_H_
