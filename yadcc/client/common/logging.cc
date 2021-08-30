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

#include "yadcc/client/common/logging.h"

namespace yadcc::client {

int min_log_level = 2;  // INFO

namespace detail {

std::string GetNow() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  auto time_now = tv.tv_sec;
  struct tm timeinfo;
  (void)localtime_r(&time_now, &timeinfo);

  return fmt::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:06d}",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                     timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                     timeinfo.tm_sec, tv.tv_usec);
}

}  // namespace detail

}  // namespace yadcc::client
