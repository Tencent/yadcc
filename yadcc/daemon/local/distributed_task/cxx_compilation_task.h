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

#ifndef YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_CXX_COMPILATION_TASK_H_
#define YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_CXX_COMPILATION_TASK_H_

#include <cstdint>
#include <string>
#include <vector>

#include "yadcc/daemon/local/distributed_task/user_task.h"
#include "yadcc/daemon/local/messages.pb.h"

namespace yadcc::daemon::local {

// Describes a C++ compilation task.
class CxxCompilationTask : public UserTask<WaitForCxxTaskResponse> {
 public:
  flare::Status Prepare(const SubmitCxxTaskRequest& request,
                        const std::vector<flare::NoncontiguousBuffer>& bytes);

  /////////////////////////////////////////////////
  // Members inherited from `DistributedTask`.   //
  /////////////////////////////////////////////////

  pid_t GetInvokerPid() const override { return requestor_pid_; }
  CacheControl GetCacheSetting() const override { return cache_control_; }
  std::string GetCacheKey() const override;
  std::string GetDigest() const override;
  const EnvironmentDesc& GetEnvironmentDesc() const override {
    return env_desc_;
  }

  flare::Expected<std::uint64_t, flare::Status> StartTask(
      const std::string& token, std::uint64_t grant_id,
      cloud::DaemonService_SyncStub* stub) override;

  Json::Value Dump() const override;

 private:
  pid_t requestor_pid_;
  CacheControl cache_control_;
  EnvironmentDesc env_desc_;
  std::string source_path_;
  std::string invocation_arguments_;
  std::string source_digest_;
  flare::NoncontiguousBuffer preprocessed_source_;  // Zstd compressed.

  flare::Expected<CxxCompilationTask::Output, flare::Status> RebuildOutput(
      const DistributedTaskOutput& output) override;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_CXX_COMPILATION_TASK_H_
