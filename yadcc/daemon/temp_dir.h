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

#ifndef YADCC_DAEMON_TEMP_DIR_H_
#define YADCC_DAEMON_TEMP_DIR_H_

#include <string>

namespace yadcc::daemon {

// Get temporary directory for use.
//
// We provide flag `--temporary_dir` for user to override this. If not
// specified, we use some heuristics to determine which directory should be
// used.
const std::string& GetTemporaryDir();

}  // namespace yadcc::daemon

#endif  // YADCC_DAEMON_TEMP_DIR_H_
