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

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "yadcc/client/command.h"
#include "yadcc/client/env_options.h"
#include "yadcc/client/logging.h"
#include "yadcc/client/task_quota.h"
#include "yadcc/client/utility.h"

using namespace std::literals;

int main(int argc, char** argv) {
  // Initialize logging first.
  yadcc::client::min_log_level = yadcc::client::GetOptionLogLevel();

  const char** real_argv = const_cast<const char**>(argv);
  int real_argc = argc;
  // In debug case. CMD seems like 'yadcc-universal-wrapper java -version'.
  if (yadcc::client::EndsWith(argv[0], "yadcc-universal-wrapper")) {
    real_argv += 1;
    real_argc -= 1;
  }

  if (real_argc == 1) {
    LOG_INFO("No compilation is requested. Leaving.");
    return 0;
  }

  if (auto pos = std::string_view(real_argv[0]).find_last_of("/");
      pos != std::string_view::npos) {
    real_argv[0] = real_argv[0] + pos + 1;
  }

  // Let's rock.
  LOG_TRACE("Started");
  auto quota = yadcc::client::AcquireTaskQuota(false);
  CHECK(quota);

  return yadcc::client::PassthroughToProgram(
      yadcc::client::FindExecutableInPath(real_argv[0]), real_argv + 1);
}
