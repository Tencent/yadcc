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

#include "yadcc/client/common/env_options.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "yadcc/client/common/logging.h"

namespace yadcc::client {

namespace {

bool GetBooleanOption(const char* name) {
  auto ptr = getenv(name);
  return ptr && (!strcmp(ptr, "1") || !strcasecmp(ptr, "y") ||
                 !strcasecmp(ptr, "yes") || !strcasecmp(ptr, "true"));
}

}  // namespace

std::size_t GetOptionCompileOnCloudSizeThreshold() {
  static const auto result = []() {
    if (auto p = getenv("YADCC_COMPILE_ON_CLOUD_SIZE_THRESHOLD")) {
      return std::stoi(p);
    }
    return 8192;  // Be conservative. Not sure if this is the right choice.
  }();
  return result;
}

int GetOptionWarnOnWaitLongerThan() {
  static const auto result = []() {
    if (auto warn_on_wait_too_long = getenv("YADCC_WARN_ON_WAIT_LONGER_THAN")) {
      int x;
      if (sscanf(warn_on_wait_too_long, "%d", &x) == 1) {
        return x;
      }
    }
    return 0;
  }();
  return result;
}

CacheControl GetOptionCacheControl() {
  static const auto result = [] {
    auto value = getenv("YADCC_CACHE_CONTROL");
    if (value) {
      auto as_int = std::stoi(value);
      CHECK(as_int >= 0 && as_int <= 2);
      return static_cast<CacheControl>(as_int);
    }
    return CacheControl::Allow;
  }();
  return result;
}

int GetOptionLogLevel() {
  static const auto result = []() {
    if (auto opt = getenv("YADCC_LOG_LEVEL"); opt) {
      return std::stoi(opt);
    }
    return 2;  // INFO by default.
  }();
  return result;
}

std::uint16_t GetOptionDaemonPort() {
  static const auto daemon = []() -> std::uint16_t {
    if (auto p = getenv("YADCC_DAEMON_PORT")) {
      return std::stoi(p);
    }
    return 8334;
  }();

  return daemon;
}

bool GetOptionIgnoreTimestampMacros() {
  static const auto result = GetBooleanOption("YADCC_IGNORE_TIMESTAMP_MACROS");
  return result;
}

bool GetOptionTreatSourceFromStdinAsLightweight() {
  static const auto result =
      GetBooleanOption("YADCC_TREAT_SOURCE_FROM_STDIN_AS_LIGHTWEIGHT");
  return result;
}

bool GetOptionWarnOnNoncacheble() {
  static const auto result = GetBooleanOption("YADCC_WARN_ON_NONCACHEABLE");
  return result;
}

bool GetOptionWarnOnNonDistributable() {
  static const auto result =
      GetBooleanOption("YADCC_WARN_ON_NON_DISTRIBUTABLE");
  return result;
}

bool GetOptionDebuggingCompileLocally() {
  static const auto result =
      GetBooleanOption("YADCC_DEBUGGING_COMPILE_LOCALLY");
  return result;
}

}  // namespace yadcc::client
