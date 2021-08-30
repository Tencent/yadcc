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

#include "yadcc/daemon/local/running_task_keeper.h"

#include <chrono>
#include <string>

#include "flare/base/chrono.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/daemon/task_digest.h"

using namespace std::literals;

namespace yadcc::daemon::local {

RunningTaskKeeper::RunningTaskKeeper() {
  sync_timer_ = flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), 1s,
                                       [this] { Refresh(); });
  last_update_time_ = flare::ReadCoarseSteadyClock();
}

RunningTaskKeeper::~RunningTaskKeeper() {}

void RunningTaskKeeper::Refresh() {
  scheduler::GetRunningTasksRequest req;
  flare::RpcClientController ctlr;
  auto result = scheduler_stub_.GetRunningTasks(req, &ctlr);

  if (!result) {
    FLARE_LOG_WARNING("Failed to get running tasks from scheduler.");
    std::scoped_lock _(lock_);
    // Clear if elapse time is over 5 seconds since last update. Avoid keeping
    // stale data in case of communication failure with the scheduler.
    if (flare::ReadCoarseSteadyClock() - last_update_time_ > 5s) {
      running_tasks_.clear();
    }
    return;
  }

  std::unordered_map<std::string, TaskDesc> tmp_running_tasks;
  for (auto&& running_task : result->running_tasks()) {
    TaskDesc task_desc = {.servant_location = running_task.servant_location(),
                          .servant_task_id = running_task.servant_task_id()};
    tmp_running_tasks[running_task.task_digest()] = std::move(task_desc);
  }

  std::scoped_lock _(lock_);
  last_update_time_ = flare::ReadCoarseSteadyClock();
  running_tasks_.swap(tmp_running_tasks);
}

std::optional<RunningTaskKeeper::TaskDesc> RunningTaskKeeper::TryFindTask(
    const std::string& task_digest) const {
  std::scoped_lock _(lock_);
  auto result = running_tasks_.find(task_digest);
  if (result != running_tasks_.end()) {
    return result->second;
  }
  return std::nullopt;
}

void RunningTaskKeeper::Stop() { flare::fiber::KillTimer(sync_timer_); }

void RunningTaskKeeper::Join() {}

}  // namespace yadcc::daemon::local
