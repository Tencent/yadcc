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

#ifndef YADCC_SCHEDULER_SCHEDULER_SERVICE_IMPL_H_
#define YADCC_SCHEDULER_SCHEDULER_SERVICE_IMPL_H_

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "yadcc/api/scheduler.flare.pb.h"
#include "yadcc/common/token_verifier.h"

namespace yadcc::scheduler {

// Implementation of `daemon.proto`.
class SchedulerServiceImpl : public SyncSchedulerService {
 public:
  SchedulerServiceImpl();

  void Heartbeat(const HeartbeatRequest& request, HeartbeatResponse* response,
                 flare::RpcServerController* controller) override;

  void GetConfig(const GetConfigRequest& request, GetConfigResponse* response,
                 flare::RpcServerController* controller) override;
  void WaitForStartingTask(const WaitForStartingTaskRequest& request,
                           WaitForStartingTaskResponse* response,
                           flare::RpcServerController* controller) override;
  void KeepTaskAlive(const KeepTaskAliveRequest& request,
                     KeepTaskAliveResponse* response,
                     flare::RpcServerController* controller) override;
  void FreeTask(const FreeTaskRequest& request, FreeTaskResponse* response,
                flare::RpcServerController* controller) override;

 private:
  std::vector<std::string> DetermineActiveServingDaemonTokens();

 private:
  std::unique_ptr<TokenVerifier> token_verifier_ = MakeTokenVerifierFromFlag();

  std::mutex lock_;
  std::chrono::steady_clock::time_point next_serving_daemon_token_rollout_{};
  // Exactly 3 tokens in this list:
  //
  // - An expiring token.
  // - An active token.
  // - A being rolled-out token.
  std::deque<std::string> active_serving_daemon_tokens_;
};

}  // namespace yadcc::scheduler

#endif  // YADCC_SCHEDULER_SCHEDULER_SERVICE_IMPL_H_
