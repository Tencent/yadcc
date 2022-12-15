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

#ifndef YADCC_DAEMON_CLOUD_DAEMON_SERVICE_IMPL_H_
#define YADCC_DAEMON_CLOUD_DAEMON_SERVICE_IMPL_H_

#include <chrono>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_set>

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/common/token_verifier.h"
#include "yadcc/daemon/cloud/execution_engine.h"

namespace yadcc::daemon::cloud {

// Implementation of `daemon.proto`.
class DaemonServiceImpl : public SyncDaemonService {
 public:
  // IP:port to access us should be provided in `network_location`.
  explicit DaemonServiceImpl(std::string network_location);

  void QueueCxxCompilationTask(const QueueCxxCompilationTaskRequest& request,
                               QueueCxxCompilationTaskResponse* response,
                               flare::RpcServerController* controller) override;

  void ReferenceTask(const ReferenceTaskRequest& request,
                     ReferenceTaskResponse* response,
                     flare::RpcServerController* controller) override;

  void WaitForCompilationOutput(
      const WaitForCompilationOutputRequest& request,
      WaitForCompilationOutputResponse* response,
      flare::RpcServerController* controller) override;

  void FreeTask(const FreeTaskRequest& request, FreeTaskResponse* response,
                flare::RpcServerController* controller) override;

  void Stop();
  void Join();

 private:
  // Send a heartbeat message to the scheduler.
  //
  // `expires_in` specifies the expected time of next heartbeat. Specifically,
  // `0` is recognized by the scheduler as a signal that we're leaving.
  void Heartbeat(std::chrono::nanoseconds expires_in);

  // Tests if a token should be accepted.
  bool IsTokenAcceptable(const std::string& token);

  // Update our token verifier with newest tokens.
  void UpdateAcceptableTokens(std::unordered_set<std::string> tokens);

 private:
  std::string network_location_;

  // This is the reason why the heart beats.
  std::uint64_t pacemaker_;

  std::shared_mutex token_lock_;
  // All tokens are rejected before this verifier is initialized.
  std::unique_ptr<TokenVerifier> token_verifier_ =
      std::make_unique<TokenVerifier>();
};

}  // namespace yadcc::daemon::cloud

#endif  // YADCC_DAEMON_CLOUD_DAEMON_SERVICE_IMPL_H_
