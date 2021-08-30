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

#ifndef YADCC_CLIENT_COMMON_ENV_OPTIONS_H_
#define YADCC_CLIENT_COMMON_ENV_OPTIONS_H_

#include <cinttypes>
#include <cstddef>

// Read options from environment variables.

namespace yadcc::client {

enum class CacheControl { Disallow = 0, Allow = 1, Refill = 2 };

// For small files not using the cloud at all will like boost overall
// performance.
//
// N.B.: The size being tested is *compressed* size of preprocessed source code.
//
// This option is read from `YADCC_COMPILE_ON_CLOUD_SIZE_THRESHOLD`.
std::size_t GetOptionCompileOnCloudSizeThreshold();

// If we've been waiting for local task quota longer than so many seconds, we
// should print a warning log.
//
// This option is read from `YADCC_WARN_ON_WAIT_LONGER_THAN`.
int GetOptionWarnOnWaitLongerThan();

// Tests if the user disabled compilation cache. Choices are:
//
// - 0: Do not use cache.
// - 1: Use cache is present (default).
// - 2: Do not use cache but fill it with our compilation result.
//
// This option is read from `YADCC_CACHE_CONTROL`.
CacheControl GetOptionCacheControl();

// Get log level set by user. If it's not set, we default to "INFO" level.
//
// This option is read from `YADCC_LOG_LEVEL`.
int GetOptionLogLevel();

// This option allows user to override port we used for calling delegate daemon.
//
// This option is read from `YADCC_DAEMON_PORT`.
std::uint16_t GetOptionDaemonPort();

// If set, we don't check for `__TIME__` / `__DATE__` / `__TIMESTAMP__` in
// preprocessed code. This allows faster preprocessing in the trade of
// inaccurate "timestamps" in the compilation result.
//
// This options is ignored (and implicitly "enabled") if these macros are
// overriden in compiler invocation arguments (some modern build toolchains
// would do this for you).
//
// This option is read from `YADCC_IGNORE_TIMESTAMP_MACROS`.
bool GetOptionIgnoreTimestampMacros();

// If set, compilation jobs reading source from `stdin` are treated as
// "lightweight" jobs. This should be the case in most situation. However, if
// used with some exotic tools, they might pipe source to GCC and leads to
// disaster.
//
// This options is read from `YADCC_TREAT_SOURCE_FROM_STDIN_AS_LIGHTWEIGHT`.
bool GetOptionTreatSourceFromStdinAsLightweight();

// For debugging purpose only. If set, non-cacheable translation unit is logged.
//
// This options is read from `YADCC_WARN_ON_NONCACHEABLE`.
bool GetOptionWarnOnNoncacheble();

// If set, yadcc emits a warning log if the task cannot be distributed to
// compile-server.
//
// Provided mostly for debugging purpose.
//
// This option it read from `YADCC_WARN_ON_NON_DISTRIBUTABLE`.
bool GetOptionWarnOnNonDistributable();

// If set, compilation is **possibly** done locally.
//
// DO NOT USE IT in production environment. The sole purpose it serves for is
// for INTERNAL debugging.
bool GetOptionDebuggingCompileLocally();

}  // namespace yadcc::client

#endif  // YADCC_CLIENT_COMMON_ENV_OPTIONS_H_
