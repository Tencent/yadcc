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

#include "yadcc/daemon/local/config_keeper.h"

#include <chrono>
#include <mutex>

#include "flare/fiber/timer.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/daemon/common_flags.h"

using namespace std::literals;

namespace yadcc::daemon::local {

ConfigKeeper::ConfigKeeper() : scheduler_stub_(FLAGS_scheduler_uri) {}

std::string ConfigKeeper::GetServingDaemonToken() const {
  std::scoped_lock _(lock_);
  return serving_daemon_token_;
}

void ConfigKeeper::Start() {
  OnFetchConfig();
  config_fetcher_ = flare::fiber::SetTimer(10s, [this] { OnFetchConfig(); });
}

void ConfigKeeper::Stop() { flare::fiber::KillTimer(config_fetcher_); }

void ConfigKeeper::Join() {
  // NOTHING.
}

void ConfigKeeper::OnFetchConfig() {
  scheduler::GetConfigRequest req;
  req.set_token(FLAGS_token);

  flare::RpcClientController ctlr;
  auto result = scheduler_stub_.GetConfig(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING("Failed to fetch config from scheduler.");
    return;
  }

  std::scoped_lock _(lock_);
  serving_daemon_token_ = result->serving_daemon_token();
}

}  // namespace yadcc::daemon::local
