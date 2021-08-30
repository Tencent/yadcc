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

#ifndef YADCC_DAEMON_CLOUD_CPU_LIMITER_H_
#define YADCC_DAEMON_CLOUD_CPU_LIMITER_H_

#include <sys/types.h>
#include <sys/vfs.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "gtest/gtest_prod.h"

#include "flare/fiber/condition_variable.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/mutex.h"

namespace yadcc::daemon::cloud {

struct ProcessInfo {
  pid_t pid;
  pid_t ppid;
  std::uint64_t cpu_time;
  std::uint64_t start_time;
  double cpu_usage;
};

// Help to limit process's cpu usage. @sa: https://github.com/opsengine/cpulimit
class CpuLimiter {
 public:
  CpuLimiter();
  ~CpuLimiter();

  // Start with the max cpu usage which processes can share.
  void StartWithMaxCpu(std::size_t max_cpu);

  // To limit cpu usage of pid.
  void Limit(pid_t pid);

  // Occupy 1 cpu and have no need to limit cpu.
  void Occupy(pid_t pid);

  // Remove the previous limit of pid.
  void Remove(pid_t pid);

  void Stop();
  void Join();

 private:
  FRIEND_TEST(DaemonService, Java);

  struct ProcessContext {
    pid_t pid;
    flare::fiber::Mutex rate_lock;
    double limit_rate;
    bool limit_rate_updated;
    std::map<pid_t, ProcessInfo> processes;
    std::vector<pid_t> living_processes;
    double working_rate;
    std::chrono::steady_clock::time_point last_update_time;
  };

 private:
  // Update cpu limit rate per process.
  void UnsafeUpdateCpuLimitRate();

  //  Endless loop to limit processes.
  void LimitLoop();

  // Use signal SIGCONT and SIGSTOP to control processes to run.
  std::int64_t StartProcess(std::shared_ptr<ProcessContext> process_context);

  // Use signal SIGCONT and SIGSTOP to control processes to run.
  void StopProcess(std::shared_ptr<ProcessContext> process_context);

  // Sample and calculate the cpu usage of processes periodically.
  void UpdateProcess(ProcessContext* process_context);

 private:
  pid_t self_pid_;
  std::atomic<std::size_t> max_cpu_ = 0;
  std::atomic<bool> exit_ = false;
  flare::Fiber worker_;

  flare::fiber::Mutex lock_;
  flare::fiber::ConditionVariable limit_cv_;
  std::map<pid_t, std::shared_ptr<ProcessContext>> contexts_;
  std::set<pid_t> occupied_processes_;
};

}  // namespace yadcc::daemon::cloud

#endif  // YADCC_DAEMON_CLOUD_CPU_LIMITER_H_
