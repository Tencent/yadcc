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

#ifndef YADCC_CLIENT_LOGGING_H_
#define YADCC_CLIENT_LOGGING_H_

#include <sys/time.h>
#include <time.h>

#include <cstdio>
#include <string>

#include "thirdparty/fmt/format.h"

namespace yadcc::client {

namespace detail {

template <class... Ts>
std::string FormatLogOpt(int /*Not used */, const Ts&... args) noexcept {
  std::string result;

  if constexpr (sizeof...(Ts) != 0) {
    result += fmt::format(args...);
  }
  return result;
}

std::string GetNow();

}  // namespace detail

// DEBUG / TRACE / INFO / WARN / ERROR: 0 ~ 5, respectively.
extern int min_log_level;

}  // namespace yadcc::client

#define YADCC_DETAIL_FORMAT_LOG(...) \
  ::yadcc::client::detail::FormatLogOpt(0, ##__VA_ARGS__).c_str()

#define YADCC_DETAIL_LOG(level_str, level, ...)                             \
  if (level >= ::yadcc::client::min_log_level) {                            \
    fprintf(stderr, "[%s] [%s] [%s:%d] %s\n",                               \
            ::yadcc::client::detail::GetNow().c_str(), level_str, __FILE__, \
            __LINE__, YADCC_DETAIL_FORMAT_LOG(__VA_ARGS__));                \
  }

#define LOG_DEBUG(...) YADCC_DETAIL_LOG("DEBUG", 0, __VA_ARGS__)
#define LOG_TRACE(...) YADCC_DETAIL_LOG("TRACE", 1, __VA_ARGS__)
#define LOG_INFO(...) YADCC_DETAIL_LOG("INFO ", 2, __VA_ARGS__)
#define LOG_WARN(...) YADCC_DETAIL_LOG("WARN ", 3, __VA_ARGS__)
#define LOG_ERROR(...) YADCC_DETAIL_LOG("ERROR", 4, __VA_ARGS__)
#define LOG_FATAL(...)                         \
  do {                                         \
    YADCC_DETAIL_LOG("FATAL", 5, __VA_ARGS__); \
    abort();                                   \
  } while (0)
#define CHECK(expr, ...)                                                 \
  if (!(expr)) {                                                         \
    std::string msg = fmt::format("Check failed: [{}]. {}", #expr,       \
                                  YADCC_DETAIL_FORMAT_LOG(__VA_ARGS__)); \
    LOG_FATAL("{}", msg);                                                \
  }
#define PCHECK(expr, ...)                                                   \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::string msg =                                                     \
          fmt::format("Check failed: [{}], {}. {}", #expr, strerror(errno), \
                      YADCC_DETAIL_FORMAT_LOG(__VA_ARGS__));                \
      LOG_FATAL("{}", msg);                                                 \
    }                                                                       \
  } while (0)

#endif  //  YADCC_CLIENT_LOGGING_H_
