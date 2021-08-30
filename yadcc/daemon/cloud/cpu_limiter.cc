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

#include "yadcc/daemon/cloud/cpu_limiter.h"

#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <string>
#include <utility>

#include "flare/base/logging.h"
#include "flare/base/string.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/this_fiber.h"

#include "yadcc/daemon/sysinfo.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

namespace {

// A constant help to update process cpu usage.
constexpr auto kAlpha = 0.08;

// Minimum control cycle.
constexpr auto kTimeSlot = 100 * 1000;

// Minimum sample interval.
constexpr auto kMinSampleInterval = 20ms;

std::optional<ProcessInfo> TryGetProcessInfo(pid_t pid) {
  static const auto kUserHz = sysconf(_SC_CLK_TCK);

  auto pid_proc = flare::Format("/proc/{}/stat", pid);
  std::ifstream file(pid_proc);
  if (!file) {
    return std::nullopt;
  }
  std::string tmp;
  ProcessInfo stat;
  stat.pid = pid;
  file >> tmp >> tmp >> tmp >> stat.ppid;
  if (stat.ppid == 0) {  // The process is leaving?
    return std::nullopt;
  }
  for (int i = 0; i < 9; ++i) {
    file >> tmp;
  }

  int i = 0;
  stat.cpu_time = 0;
  while (i++ < 2) {
    file >> tmp;
    auto cpu_time = flare::TryParse<std::size_t>(tmp);
    if (!cpu_time) {
      return std::nullopt;
    }
    stat.cpu_time += (*cpu_time) * 1000 / kUserHz;
  }

  for (int i = 0; i < 7; ++i) {
    file >> tmp;
  }
  if (!file) {
    return std::nullopt;
  }

  auto start_time = flare::TryParse<std::size_t>(tmp);
  if (!start_time) {
    return std::nullopt;
  }
  stat.start_time = *start_time / kUserHz;

  FLARE_CHECK_GT(stat.ppid, 0);
  return stat;
}

std::optional<pid_t> TryGetParentPid(pid_t pid) {
  auto pid_proc = flare::Format("/proc/{}/stat", pid);
  std::ifstream file(pid_proc);
  if (!file) {
    return std::nullopt;
  }
  std::string tmp;
  pid_t ppid = -1;
  file >> tmp >> tmp >> tmp >> ppid;
  if (!file) {
    return std::nullopt;
  }
  if (ppid == 0) {  // The process is leaving?
    return std::nullopt;
  }
  FLARE_CHECK_GT(ppid, 0);
  return ppid;
}

bool IsChildOf(pid_t child, pid_t parent) {
  std::optional<pid_t> ppid = child;
  while (*ppid != parent) {
    ppid = TryGetParentPid(*ppid);
    if (!ppid) {
      return false;
    }
  }
  return *ppid == parent;
}

}  // namespace

CpuLimiter::CpuLimiter() : self_pid_(getpid()) {
  FLARE_CHECK(self_pid_ > 0);
  struct statfs mnt;
  FLARE_CHECK(statfs("/proc", &mnt) == 0);
  FLARE_CHECK(mnt.f_type == 0x9fa0);
}

CpuLimiter::~CpuLimiter() {}

void CpuLimiter::StartWithMaxCpu(std::size_t max_cpu) {
  FLARE_CHECK(max_cpu > 0);
  FLARE_CHECK(max_cpu_ == 0);
  max_cpu_ = max_cpu;
  worker_ = flare::Fiber([this] { LimitLoop(); });
}

void CpuLimiter::Limit(pid_t pid) {
  FLARE_CHECK(pid > 0 && pid != self_pid_);
  FLARE_CHECK(max_cpu_ > 0);
  if (kill(pid, 0) != 0) {
    return;
  }
  std::scoped_lock _(lock_);
  if (contexts_.count(pid) != 0 || occupied_processes_.count(pid) != 0) {
    return;
  }

  auto context = std::make_shared<ProcessContext>();
  context->pid = pid;
  context->limit_rate_updated = false;
  context->working_rate = -1;
  contexts_.emplace(pid, context);
  UnsafeUpdateCpuLimitRate();
  limit_cv_.notify_all();
}

void CpuLimiter::Occupy(pid_t pid) {
  std::scoped_lock _(lock_);
  if (contexts_.count(pid) != 0 || occupied_processes_.count(pid) != 0) {
    return;
  }
  occupied_processes_.insert(pid);
  UnsafeUpdateCpuLimitRate();
}

void CpuLimiter::Remove(pid_t pid) {
  std::scoped_lock _(lock_);
  auto it = contexts_.find(pid);
  if (it != contexts_.end()) {
    contexts_.erase(it);
    UnsafeUpdateCpuLimitRate();
  } else if (occupied_processes_.count(pid) != 0) {
    occupied_processes_.erase(pid);
    UnsafeUpdateCpuLimitRate();
  }
}

void CpuLimiter::Stop() { exit_.store(true, std::memory_order_relaxed); }

void CpuLimiter::Join() { worker_.join(); }

void CpuLimiter::UnsafeUpdateCpuLimitRate() {
  if (contexts_.empty()) {
    return;
  }
  auto rate_per_proc =
      (max_cpu_ - occupied_processes_.size()) * 1.0 / contexts_.size();
  for (auto&& [_, context] : contexts_) {
    std::scoped_lock __(context->rate_lock);
    context->limit_rate = rate_per_proc;
    context->limit_rate_updated = true;
  }
}

void CpuLimiter::LimitLoop() {
  while (!exit_.load(std::memory_order_relaxed)) {
    std::vector<std::shared_ptr<ProcessContext>> contexts;
    {
      std::unique_lock lk(lock_);
      if (!limit_cv_.wait_for(lk, 1s, [=] { return !contexts_.empty(); })) {
        continue;
      }
      for (auto&& [_, context] : contexts_) {
        contexts.push_back(context);
      }
    }

    std::deque<std::pair<std::int64_t, std::shared_ptr<ProcessContext>>>
        run_queue;
    for (auto&& context : contexts) {
      auto time_to_work = StartProcess(context);
      if (time_to_work == -1 && time_to_work >= kTimeSlot) {
        continue;
      }
      run_queue.emplace_back(time_to_work, context);
    }

    std::sort(
        run_queue.begin(), run_queue.end(),
        [](const std::pair<std::int64_t, std::shared_ptr<ProcessContext>>& lhs,
           const std::pair<std::int64_t, std::shared_ptr<ProcessContext>>&
               rhs) { return lhs.first < rhs.first; });

    if (!run_queue.empty()) {
      auto time_to_sleep = kTimeSlot - run_queue.back().first;
      while (!run_queue.empty()) {
        auto next_stop_time = run_queue.front().first;
        flare::this_fiber::SleepFor(next_stop_time * 1us);
        StopProcess(run_queue.front().second);
        run_queue.pop_front();
        run_queue.front().first -= next_stop_time;
      }
      flare::this_fiber::SleepFor(time_to_sleep * 1us);
    } else {
      flare::this_fiber::SleepFor(kTimeSlot * 1us);
    }
  }
}

std::int64_t CpuLimiter::StartProcess(
    std::shared_ptr<ProcessContext> process_context) {
  double limit_rate = 0;
  {
    std::scoped_lock _(process_context->rate_lock);
    if (process_context->limit_rate_updated) {
      // Becuse of a change of limit rate, we must accumulate cpu usage from
      // now on.
      process_context->processes.clear();
      process_context->living_processes.clear();
      process_context->limit_rate_updated = false;
      process_context->working_rate = -1;
      process_context->last_update_time =
          std::chrono::steady_clock::time_point();
    }
    limit_rate = process_context->limit_rate;
  }

  UpdateProcess(process_context.get());
  // Maybe no relative processes are alive.
  if (process_context->living_processes.empty()) {
    return -1;
  }

  auto time_to_work = kTimeSlot;
  double total_cpu_usage = -1;
  for (auto pid : process_context->living_processes) {
    auto cpu_usage = process_context->processes.at(pid).cpu_usage;
    if (cpu_usage < 0) {
      continue;
    }
    total_cpu_usage = std::max(total_cpu_usage, 0.0);
    total_cpu_usage += cpu_usage;
  }

  // First cycle.
  if (total_cpu_usage <= 0) {
    total_cpu_usage = limit_rate;
    process_context->working_rate = limit_rate;
  } else {
    process_context->working_rate = std::min(
        process_context->working_rate / total_cpu_usage * limit_rate, 1.0);
  }
  time_to_work *= process_context->working_rate;

  // Time to run the processes.
  for (auto it = process_context->living_processes.begin();
       it != process_context->living_processes.end();) {
    if (kill(*it, SIGCONT) != 0) {
      FLARE_LOG_WARNING_EVERY_SECOND("Send SIGCONT failed, pid[{}], errno[{}]",
                                     *it, errno);
      process_context->processes.erase(*it);
      it = process_context->living_processes.erase(it);
    } else {
      ++it;
    }
  }

  return time_to_work > 0 ? time_to_work : 0;
}

void CpuLimiter::StopProcess(std::shared_ptr<ProcessContext> process_context) {
  for (auto&& pid : process_context->living_processes) {
    if (kill(pid, SIGSTOP) != 0) {
      process_context->processes.erase(pid);
    }
  }
}

void CpuLimiter::UpdateProcess(ProcessContext* process_context) {
  process_context->living_processes.clear();
  std::unique_ptr<DIR, int (*)(DIR*)> dir{opendir("/proc"), &closedir};
  auto time_diff =
      flare::ReadCoarseSteadyClock() - process_context->last_update_time;
  while (auto ent = readdir(dir.get())) {
    auto pid = flare::TryParse<pid_t>(ent->d_name);
    if (!pid || *pid != process_context->pid ||
        !IsChildOf(*pid, process_context->pid)) {
      continue;
    }
    auto process_info = TryGetProcessInfo(*pid);
    if (!process_info) {
      process_context->processes.erase(*pid);
      continue;
    }

    auto it = process_context->processes.find(*pid);
    if (it != process_context->processes.end()) {
      // Start time is inconsistent with previous. We thought this might be a
      // completely new process due to PID reusing. It depends on the
      // implementation of the Linux kernel.
      if (process_info->start_time != it->second.start_time) {
        it->second = *process_info;
        it->second.cpu_usage = -1;
        continue;
      }

      if (time_diff < kMinSampleInterval) {
        continue;
      }
      double sample = 1.0 * (process_info->cpu_time - it->second.cpu_time) /
                      (time_diff / 1ms);
      if (it->second.cpu_usage == -1) {
        it->second.cpu_usage = sample;
      } else {
        it->second.cpu_usage =
            (1.0 - kAlpha) * it->second.cpu_usage + kAlpha * sample;
      }
      it->second.cpu_time = process_info->cpu_time;
    } else {
      process_info->cpu_usage = -1;
      process_context->processes.emplace(*pid, *process_info);
    }
    process_context->living_processes.push_back(*pid);
  }
  process_context->last_update_time = flare::ReadCoarseSteadyClock();
}

}  // namespace yadcc::daemon::cloud
