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

#ifndef YADCC_DAEMON_LOCAL_RUNNING_TASK_KEEPER_H_
#define YADCC_DAEMON_LOCAL_RUNNING_TASK_KEEPER_H_

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/api/scheduler.flare.pb.h"
#include "yadcc/daemon/common_flags.h"

namespace yadcc::daemon::local {

// This class is designed to synchronize running task information from the
// scheduler and reduce duplicate compilation of the same task.
class RunningTaskKeeper {
 public:
  RunningTaskKeeper();
  ~RunningTaskKeeper();

  struct TaskDesc {
    std::string servant_location;
    std::uint64_t servant_task_id;
  };

  // If the compilation task has already existed on cluster, we find where is it
  // first.
  std::optional<TaskDesc> TryFindTask(const std::string& task_digest) const;

  void Stop();
  void Join();

 private:
  void Refresh();

 private:
  scheduler::SchedulerService_SyncStub scheduler_stub_{FLAGS_scheduler_uri};

  mutable std::mutex lock_;
  std::unordered_map<std::string, TaskDesc> running_tasks_;
  std::chrono::steady_clock::time_point last_update_time_;
  std::uint64_t sync_timer_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_RUNNING_TASK_KEEPER_H_
