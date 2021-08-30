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

#ifndef YADCC_DAEMON_LOCAL_CONFIG_KEEPER_H_
#define YADCC_DAEMON_LOCAL_CONFIG_KEEPER_H_

#include <cstdint>
#include <mutex>
#include <string>

#include "yadcc/api/scheduler.flare.pb.h"

namespace yadcc::daemon::local {

// This class maintains cluster-wide configurations.
class ConfigKeeper {
 public:
  ConfigKeeper();

  // Token for contacting other daemons. (But not the scheduler, nor the cache
  // server.).
  std::string GetServingDaemonToken() const;

  void Start();
  void Stop();
  void Join();

 private:
  void OnFetchConfig();

 private:
  std::uint64_t config_fetcher_;
  scheduler::SchedulerService_SyncStub scheduler_stub_;

  mutable std::mutex lock_;
  std::string serving_daemon_token_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_CONFIG_KEEPER_H_
