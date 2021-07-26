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

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/base/logging.h"
#include "flare/testing/main.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

TEST(Sysinfo, All) {
  InitializeSystemInfo();

  // Waiting for samples enough.
  std::this_thread::sleep_for(5s);

  std::thread thread([] {
    auto count = 3;
    while (count-- != 0) {
      auto v = TryGetProcessorLoad(2s);
      ASSERT_TRUE(v);
      EXPECT_GE(*v, 0);
      FLARE_LOG_INFO("Processor load: {}", *v);
      std::this_thread::sleep_for(2s);
    }
  });
  thread.join();
  ShutdownSystemInfo();
}

TEST(Sysinfo, Loadavg) {
  static const auto kThreads = std::min<int>(5, GetNumberOfProcessors());

  std::atomic<bool> shutdown = false;
  InitializeSystemInfo();
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] {
      while (!shutdown) {
      }
    });
  }

  // Waiting for samples enough.
  std::this_thread::sleep_for(2s);

  std::thread thread([&] {
    auto count = 10;
    while (count-- != 0) {
      auto v = TryGetProcessorLoad(1s);
      ASSERT_TRUE(v);
      EXPECT_GE(*v, threads.size());
      FLARE_LOG_INFO("Processor load: {}", *v);
      std::this_thread::sleep_for(1s);
    }
  });
  thread.join();

  shutdown = true;
  for (auto& t : threads) {
    t.join();
  }
  threads.clear();
  ShutdownSystemInfo();
}

TEST(Sysinfo, Memory) {
  constexpr std::size_t kBufferSize = 2ULL * 1024 * 1024 * 1024;
  std::size_t mem_available1, mem_available2;
  mem_available1 = GetMemoryAvailable();
  std::unique_ptr<char[]> buffer = std::make_unique<char[]>(kBufferSize);
  mem_available2 = GetMemoryAvailable() + kBufferSize;
  EXPECT_NEAR(mem_available1, mem_available2, mem_available2 / 10);
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
