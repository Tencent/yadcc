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

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace std::literals;

// This program helps us to test cpu limiter.
int main(int argc, char** argv) {
  int time_to_run = 5;

  int nthread = 4;
  if (argc > 1) {
    time_to_run = std::stoi(argv[1]);
    if (time_to_run <= 0) {
      return 1;
    }
  }

  if (argc > 2) {
    nthread = std::stoi(argv[2]);
    if (nthread <= 0) {
      return 1;
    }
  }

  std::vector<std::thread> group;
  std::atomic<bool> exit = false;

  for (int i = 0; i < nthread - 1; ++i) {
    group.emplace_back([&, i] {
      while (!exit.load(std::memory_order_relaxed)) {
      }
    });
  }

  auto start = std::chrono::system_clock::now();
  while (std::chrono::system_clock::now() - start < time_to_run * 1s) {
  }

  exit.store(true, std::memory_order_relaxed);

  for (auto&& t : group) {
    t.join();
  }
  return 0;
}
