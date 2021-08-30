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

#ifndef YADCC_CLIENT_COMMON_DAEMON_CALL_H_
#define YADCC_CLIENT_COMMON_DAEMON_CALL_H_

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yadcc::client {

struct DaemonResponse {
  int status;  // HTTP status code, or negative if failed catastrophically.
  std::string body;
};

// Call our local daemon using a dirty HTTP client. `localhost` is implied.
//
// If gather I/O is desired, you can supply multiple buffers via `bodies`.
//
// TODO(luobogao): Not sure if we want to use a private binary protocol instead.
// That would require a dirty-and-quick RPC framework.
DaemonResponse DaemonCall(const std::string& api,
                          const std::vector<std::string>& headers,
                          const std::string& body,
                          std::chrono::nanoseconds timeout);
DaemonResponse DaemonCallGathered(const std::string& api,
                                  const std::vector<std::string>& headers,
                                  const std::vector<std::string_view>& bodies,
                                  std::chrono::nanoseconds timeout);

using DaemonCallGatheredHandler = std::function<DaemonResponse(
    const std::string&, const std::vector<std::string>&,
    const std::vector<std::string_view>&, std::chrono::nanoseconds)>;

// For testing purpose only. This allow you to generate a mock response for
// `DaemonCall`.
void SetDaemonCallGatheredHandler(DaemonCallGatheredHandler handler);

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMMON_DAEMON_CALL_H_
