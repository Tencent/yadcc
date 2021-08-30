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

#include "yadcc/daemon/sysinfo.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "flare/base/logging.h"
#include "flare/testing/main.h"

using namespace std::literals;

namespace {

constexpr std::size_t kTestFileSize = 200ULL * 1024 * 1024;

char buffer[kTestFileSize];

void CreateEmptyFile(const std::string& path) {
  memset(buffer, 'a', sizeof(buffer));
  FILE* fp = fopen(path.c_str(), "wb");
  fwrite(buffer, 1, sizeof(buffer), fp);
  fclose(fp);
}

}  // namespace

namespace yadcc::daemon {

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

TEST(Sysinfo, DirSpace) {
  int pass_test = 0;
  for (int i = 0; i < 100; ++i) {
    std::size_t dir_available1 = GetDiskAvailableSize(".");
    CreateEmptyFile("1");
    std::size_t dir_available2 = GetDiskAvailableSize(".");
    unlink("1");
    FLARE_LOG_ERROR("size1: {}, size2: {}, size1 - size2: {}, diff: {}",
                    dir_available1, dir_available2,
                    dir_available1 - dir_available2,
                    dir_available1 - dir_available2 - kTestFileSize);
    if (abs(static_cast<int64_t>(dir_available1 - dir_available2 -
                                 kTestFileSize)) < 10485760) {
      ++pass_test;
    }
  }
  EXPECT_GT(pass_test, 90);
}

}  // namespace yadcc::daemon

FLARE_TEST_MAIN
