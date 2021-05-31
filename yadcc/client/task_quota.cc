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

#include "yadcc/client/task_quota.h"

#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <thread>

#include "thirdparty/fmt/format.h"

#include "yadcc/client/daemon_call.h"
#include "yadcc/client/env_options.h"
#include "yadcc/client/logging.h"

using namespace std::literals;

namespace yadcc::client {

void ReleaseTaskQuota() {
  auto body = fmt::format("{{\"requestor_pid\": {}}}", getpid());

  // Failure (if any) is ignored. The daemon should be able to handle the case
  // we crashed without releasing the quota anyway.
  DaemonCall("/local/release_quota", {"Content-Type: application/json"}, body,
             5s);
}

std::shared_ptr<void> TryAcquireTaskQuota(bool lightweight_task,
                                          std::chrono::nanoseconds timeout) {
  auto body = fmt::format(
      "{{\"milliseconds_to_wait\": {}, \"lightweight_task\": {}, "
      "\"requestor_pid\": {}}}",
      timeout / 1ms, lightweight_task ? "true" : "false", getpid());

  auto&& [status, resp_body] = DaemonCall(
      "/local/acquire_quota", {"Content-Type: application/json"}, body,
      15s /* Must be greater than `milliseconds_to_wait` in HTTP request */);

  if (status == 200) {  // Quota granted.
    // Nothing useful to us in `resp_body`.
    return std::shared_ptr<void>(reinterpret_cast<void*>(1),
                                 [](auto) { ReleaseTaskQuota(); });
  } else if (status == 503) {
    // NOTHING.
  } else if (status == -1) {  // HTTP request itself failed?
    LOG_ERROR("Cannot contact delegate daemon. Daemon died?");
    std::this_thread::sleep_for(1s);
    // TODO(luobogao): Start delegate daemon automatically.
  } else {
    LOG_ERROR("Unexpected HTTP status code [{}] from delegate daemon: {}",
              status, body);
    std::this_thread::sleep_for(1s);
  }

  // TODO(luobogao): Handle the case when daemon is unresponsive.
  return nullptr;
}

std::shared_ptr<void> AcquireTaskQuota(bool lightweight_task) {
  auto start = std::chrono::steady_clock::now();

  while (true) {
    auto acquired = TryAcquireTaskQuota(lightweight_task, 10s);
    if (acquired) {
      return acquired;
    }

    // TODO(luobogao): If we've been waiting for quite a long time and the
    // reason why we're here was a transient failure, we might want to retry
    // compilation on the cloud.
    auto threshold = GetOptionWarnOnWaitLongerThan();
    auto waited = std::chrono::steady_clock::now() - start;
    if (threshold && waited / 1s > threshold) {
      LOG_WARN(
          "Can't get permission to start new task from delegate daemon after "
          "{} seconds. Overloaded?",
          waited / 1s);
    }
  }
}

}  // namespace yadcc::client
