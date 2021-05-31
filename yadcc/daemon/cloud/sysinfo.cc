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

#include "yadcc/daemon/cloud/sysinfo.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "flare/base/chrono.h"
#include "flare/base/internal/cpu.h"
#include "flare/base/logging.h"
#include "flare/base/string.h"
#include "flare/fiber/timer.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

namespace {

struct ProcMemInfo {
  std::int64_t mem_total = -1;
  std::int64_t mem_free = -1;
  std::int64_t mem_available = -1;  // since 3.14
  std::int64_t buffers = -1;
  std::int64_t cached = -1;
  std::int64_t swap_total = -1;
  std::int64_t swap_free = -1;
};

// We onliy keep 61 samples. It`s enough.
constexpr auto kSampleCount = 61;

std::mutex sys_uptime_samples_lock_;

std::uint64_t sample_timer_;

std::deque<double> sys_uptime_samples_;

double GetProcessorIdleTime() {
  static const auto kUserHz = sysconf(_SC_CLK_TCK);

  // @sa: https://man7.org/linux/man-pages/man5/proc.5.html
  std::ifstream ifile("/proc/stat");
  FLARE_LOG_FATAL_IF(!ifile,
                     "Open '/proc/stat' failed, so we can`t sample sys uptime");
  double temp, idle;
  std::string s;
  ifile >> s >> temp >> temp >> temp >> idle;
  idle /= kUserHz;
  FLARE_CHECK_GT(idle, 0.0);
  return idle;
}

ProcMemInfo GetProcMemInfo() {
  ProcMemInfo info;
  std::ifstream in("/proc/meminfo");
  FLARE_CHECK(in);

  std::string buf;
  while (std::getline(in, buf)) {
    char key[101];
    std::int64_t value;
    auto n = sscanf(buf.c_str(), "%100s%" SCNd64, key, &value);
    if (n != 2) {
      FLARE_LOG_WARNING("Fail to sscanf: {}", std::string(buf));
      continue;
    }

    if (flare::StartsWith(key, "MemTotal:")) {
      info.mem_total = value;
    } else if (flare::StartsWith(key, "MemFree:")) {
      info.mem_free = value;
    } else if (flare::StartsWith(key, "MemAvailable:")) {
      info.mem_available = value;
    } else if (flare::StartsWith(key, "Buffers:")) {
      info.buffers = value;
    } else if (flare::StartsWith(key, "Cached:")) {
      info.cached = value;
    } else if (flare::StartsWith(key, "SwapTotal:")) {
      info.swap_total = value;
    } else if (flare::StartsWith(key, "SwapFree:")) {
      info.swap_free = value;
    }
  }

  FLARE_CHECK(in.eof());
  return info;
}

}  // namespace

void SampleProcessorIdleTime() {
  std::scoped_lock _(sys_uptime_samples_lock_);
  sys_uptime_samples_.emplace_back(GetProcessorIdleTime());
  if (sys_uptime_samples_.size() > kSampleCount) {
    sys_uptime_samples_.pop_front();
  }
}

// We sample sys uptime periodically to get finer-grained sys load average.
void InitializeSystemInfo() {
  // We sample the sys uptime every 1s.
  sample_timer_ = flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), 1s,
                                         [] { SampleProcessorIdleTime(); });
}

void ShutdownSystemInfo() { flare::fiber::KillTimer(sample_timer_); }

std::size_t GetNumberOfProcessors() {
  // We do not support CPU hot-plugin, so we use `static` here.
  static const auto num = flare::internal::GetNumberOfProcessorsAvailable();
  return num;
}

std::optional<std::size_t> TryGetProcessorLoad(std::chrono::seconds duration) {
  std::scoped_lock _(sys_uptime_samples_lock_);
  auto interval = duration / 1s;
  if (interval < sys_uptime_samples_.size()) {
    auto start = sys_uptime_samples_[sys_uptime_samples_.size() - 1 - interval];
    auto end = sys_uptime_samples_.back();
    auto idle_cores = (end - start) / interval;
    return GetNumberOfProcessors() - std::floor(idle_cores);
  }
  return std::nullopt;
}

std::size_t GetProcessorLoadInLastMinute() {
  double loadavg = 0.0;
  // We are interested in first load average
  FLARE_PCHECK(getloadavg(&loadavg, 1) == 1);
  return static_cast<std::size_t>(std::ceil(loadavg));
}

std::size_t GetMemoryAvailable() {
  ProcMemInfo info = GetProcMemInfo();
  return info.mem_available * 1024;
}

std::size_t GetTotalMemory() {
  ProcMemInfo info = GetProcMemInfo();
  return info.mem_total * 1024;
}

}  // namespace yadcc::daemon::cloud
