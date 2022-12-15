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

#ifndef YADCC_DAEMON_CLOUD_REMOTE_TASK_CXX_COMPILATION_TASK_H_
#define YADCC_DAEMON_CLOUD_REMOTE_TASK_CXX_COMPILATION_TASK_H_

#include <string>

#include "gtest/gtest_prod.h"

#include "flare/base/future.h"
#include "flare/base/status.h"

#include "yadcc/api/daemon.pb.h"
#include "yadcc/daemon/cloud/remote_task.h"
#include "yadcc/daemon/cloud/temporary_dir.h"

namespace yadcc::daemon::cloud {

// Implements remote C++ compilation.
class CxxCompilationTask : public RemoteTask {
 public:
  CxxCompilationTask();

  // For the simplicity of programming, we use the original request here.
  flare::Status Prepare(const QueueCxxCompilationTaskRequest& request,
                        const flare::NoncontiguousBuffer& attachment);

  std::string GetCommandLine() const override;
  flare::NoncontiguousBuffer GetStandardInputOnce() override;
  Json::Value DumpInternals() const override;

  std::string GetDigest() const override;
  std::optional<std::string> GetCacheKey() const override;

 protected:
  flare::Expected<OobOutput, flare::Status> GetOobOutput(
      int exit_code, const std::string& standard_output,
      const std::string& standard_error) override;

 private:
  FRIEND_TEST(CxxCompilationTask, All);

  TemporaryDir workspace_dir_;
  std::string source_path_;
  // Basic facts about the request.
  std::string command_line_;
  flare::NoncontiguousBuffer source_;
  EnvironmentDesc env_desc_;
  std::string invocation_arguments_;
  std::string source_digest_;

  flare::Future<bool> write_cache_future_;
  bool write_cache_;  // Filled in `GetOobOutput`.

  // Relative to `temporary_dir`. These extra subdirs help us to make full
  // path long (so that we have sufficient space to replace the path later on.).
  //
  // @sa: MakeLongLongRelativePathWith
  std::string temporary_dir_extra_depth_;
};

}  // namespace yadcc::daemon::cloud

#endif  // YADCC_DAEMON_CLOUD_REMOTE_TASK_CXX_COMPILATION_TASK_H_
