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

#ifndef YADCC_DAEMON_LOCAL_TASK_GRANT_KEEPER_H_
#define YADCC_DAEMON_LOCAL_TASK_GRANT_KEEPER_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>

#include "flare/fiber/condition_variable.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/mutex.h"

#include "yadcc/api/scheduler.flare.pb.h"

namespace yadcc::daemon::local {

// This class helps us to grab, and if necessary, prefetch grants for starting
// new tasks, from our scheduler.
class TaskGrantKeeper {
 public:
  // Describes a task grant alloacted by the scheduler.
  struct GrantDesc {
    // Network delay has been compensated by the fetcher by substracting a
    // period of time from `expires_at`. Don't do that yourself again.
    std::chrono::steady_clock::time_point expires_at;
    std::uint64_t grant_id;
    std::string servant_location;

    bool operator<(const GrantDesc& other) const {
      return expires_at > other.expires_at;
    }
  };

  TaskGrantKeeper();

  // Grab a grant for starting new task.
  std::optional<GrantDesc> Get(const EnvironmentDesc& desc,
                               const std::chrono::nanoseconds& timeout);

  // Free a previous allocated grant.
  void Free(std::uint64_t grant_id);

  void Stop();
  void Join();

 private:
  struct PerEnvGrantKeeper;

  // Wake up the fiber for fetching new grants.
  void WakeGrantFetcherFor(const EnvironmentDesc& desc);

  void GrantFetcherProc(PerEnvGrantKeeper* keeper);

 private:
  struct PerEnvGrantKeeper {
    EnvironmentDesc env_desc;  // Our environment.

    flare::fiber::Mutex lock;
    flare::fiber::ConditionVariable need_more_cv, available_cv;

    // Number of waiters waiting on us.
    int waiters = 0;

    // Available grants. They'll either be handed to `waiters`, or in case we
    // have spare ones, save here.
    //
    // Besides, if we prefetched some grants, they're saved here too.
    // Prefetching helps to reduce latency in critical path.
    std::queue<GrantDesc> remaining;

    // Fiber for fetching more grants.
    flare::Fiber fetcher;
  };

  scheduler::SchedulerService_AsyncStub scheduler_stub_;

  flare::fiber::Mutex lock_;
  std::atomic<bool> leaving_ = false;

  // We never clean up this map, in case the client keep sending us random
  // environment, this will be a DoS vulnerability.
  std::unordered_map<std::string, std::unique_ptr<PerEnvGrantKeeper>> keepers_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_TASK_GRANT_KEEPER_H_
