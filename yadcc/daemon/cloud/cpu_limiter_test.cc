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

#include <limits.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "gtest/gtest.h"

#include "flare/base/chrono.h"
#include "flare/base/logging.h"
#include "flare/base/string.h"
#include "flare/testing/main.h"

#include "yadcc/daemon/sysinfo.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

std::uint64_t GetProcessCpuTick(pid_t pid) {
  static const auto kUserHz = sysconf(_SC_CLK_TCK);

  auto pid_proc = flare::Format("/proc/{}/stat", pid);
  std::ifstream file(pid_proc);
  EXPECT_TRUE(!!file);
  std::string tmp;
  for (int i = 0; i < 13; ++i) {
    file >> tmp;
  }
  auto cpu_tick = 0;
  int i = 0;
  while (i++ < 2) {
    file >> tmp;
    auto tick = flare::TryParse<std::size_t>(tmp);
    EXPECT_TRUE(!!tick);
    cpu_tick += *tick * 1000 / kUserHz;
  }
  return cpu_tick;
}

pid_t Spawn() {
  pid_t pid = fork();
  EXPECT_GE(pid, 0);
  if (pid == 0) {
    std::string pwd = get_current_dir_name();
    std::string test_file = std::move(pwd);
    test_file.append("/build64_release/yadcc/daemon/cloud");
    test_file.append("/cpu_limiter_test_main");
    FLARE_LOG_INFO("test_file[{}]", test_file);
    if (execl(test_file.c_str(), "", "10", "4", nullptr) != 0) {
      std::abort();
    }
  }
  return pid;
}

std::uint64_t TestPidCpu(pid_t pid, double expect, std::uint64_t tick_ever_used,
                         std::chrono::steady_clock::time_point start) {
  auto cpu_tick = GetProcessCpuTick(pid);
  auto elapse = flare::ReadCoarseSteadyClock() - start;
  auto cpu_usage = 1.0 * (cpu_tick - tick_ever_used) / (elapse / 1ms);
  FLARE_LOG_INFO("Child process cpu usage[{}]", cpu_usage);
  EXPECT_GE(cpu_usage, expect - 0.5);
  EXPECT_LE(cpu_usage, expect + 0.1);
  return cpu_tick;
}

const auto kMaxCpuCore = 4;

TEST(CpuLimiter, Occupy) {
  pid_t pid = Spawn();
  auto start = flare::ReadCoarseSteadyClock();
  CpuLimiter cpu_limiter;
  cpu_limiter.StartWithMaxCpu(kMaxCpuCore);
  cpu_limiter.Limit(pid);
  std::this_thread::sleep_for(2s);
  auto cpu_tick = TestPidCpu(pid, kMaxCpuCore, 0, start);

  cpu_limiter.Occupy(-1);
  start = flare::ReadCoarseSteadyClock();
  std::this_thread::sleep_for(8s);
  TestPidCpu(pid, kMaxCpuCore - 1, cpu_tick, start);

  waitpid(pid, nullptr, 0);
  cpu_limiter.Remove(pid);
  cpu_limiter.Remove(-1);
  cpu_limiter.Stop();
  cpu_limiter.Join();
}

TEST(CpuLimiter, Limit2) {
  pid_t pid = Spawn();
  auto start = flare::ReadCoarseSteadyClock();
  CpuLimiter cpu_limiter;
  cpu_limiter.StartWithMaxCpu(kMaxCpuCore);
  cpu_limiter.Limit(pid);
  std::this_thread::sleep_for(2s);
  auto cpu_tick = TestPidCpu(pid, kMaxCpuCore, 0, start);

  pid_t pid2 = Spawn();
  start = flare::ReadCoarseSteadyClock();
  cpu_limiter.Limit(pid2);
  std::this_thread::sleep_for(8s);
  TestPidCpu(pid, kMaxCpuCore / 2, cpu_tick, start);
  TestPidCpu(pid2, kMaxCpuCore / 2, 0, start);

  waitpid(pid, nullptr, 0);
  waitpid(pid2, nullptr, 0);
  cpu_limiter.Remove(pid);
  cpu_limiter.Remove(pid2);
  cpu_limiter.Stop();
  cpu_limiter.Join();
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
