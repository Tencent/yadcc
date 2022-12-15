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

#ifndef YADCC_SCHEDULER_RUNNING_TASK_BOOKKEEPER_H_
#define YADCC_SCHEDULER_RUNNING_TASK_BOOKKEEPER_H_

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "yadcc/api/scheduler.pb.h"

namespace yadcc::scheduler {

// We collect running tasks info from daemons and distribute them over cluster.
class RunningTaskBookkeeper {
 public:
  // There is one daemon reporting it`s running task info, so we merge it.
  void SetServantRunningTasks(const std::string& servant_location,
                              std::vector<RunningTask> tasks);

  // Drop one servant's running task info.
  void DropServant(const std::string& servant_location);

  // Share all running tasks of cluster with daemons.
  std::vector<RunningTask> GetRunningTasks() const;

 private:
  mutable std::mutex lock_;
  std::unordered_map<std::string, std::vector<RunningTask>> running_tasks_;
};

}  // namespace yadcc::scheduler

#endif  // YADCC_SCHEDULER_RUNNING_TASK_BOOKKEEPER_H_
