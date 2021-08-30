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

#ifndef YADCC_DAEMON_CLOUD_REMOTE_TASK_H_
#define YADCC_DAEMON_CLOUD_REMOTE_TASK_H_

#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/any.pb.h"

#include "flare/base/buffer.h"
#include "flare/base/expected.h"
#include "flare/base/status.h"

#include "yadcc/daemon/cloud/execution_task.h"

namespace yadcc::daemon::cloud {

// This class helps you to implement task submitted by other machines.
//
// You shouldn't implement methods inherited from `ExecutionTask` unless they're
// also listed in this class.
class RemoteTask : public ExecutionTask {
 public:
  /////////////////////////////////////////////
  // Inherited from `ExecutionTask` and left //
  // for you to implement.                   //
  /////////////////////////////////////////////

  virtual std::string GetCommandLine() const = 0;
  virtual flare::NoncontiguousBuffer GetStandardInputOnce() = 0;
  virtual Json::Value DumpInternals() const = 0;

  // All other methods inherited from `ExecutionTask` is implemented by this
  // class, don't override them.

  //////////////////////////////////////////
  // New methods required by this class.  //
  //////////////////////////////////////////

  // Get digest of this task.
  virtual std::string GetDigest() const = 0;

  // Get cache key of this task.
  //
  // If caching should not be done, this method returns `std::nullopt`.
  //
  // This method is not called prior to `GetOobOutput`. (This convention might
  // provide extra optimization possibilities in certain case.)
  virtual std::optional<std::string> GetCacheKey() const = 0;

  //////////////////////////////////////////////////////////
  // Methods below are provided by this class.            //
  // They may not be called until the task has completed. //
  //////////////////////////////////////////////////////////

  // Get exit code of the command.
  int GetExitCode() const;

  // Get stdout / stderr written by the command.
  const std::string& GetStandardOutput() const;
  const std::string& GetStandardError() const;

  // Get OOB output produced by `GetOobOutput`.
  const google::protobuf::Any& GetExtraInfo() const;

  // Get files produced by `GetOobOutput`. The files are packed as KV-pairs,
  // with values compressed with zstd.
  const flare::NoncontiguousBuffer& GetOutputFilePack() const;

 protected:
  struct OobOutput {
    // Meaning of this field is task specific.
    google::protobuf::Any extra_info;

    // Meaning of keys are task specific, values are raw (uncompressed) file
    // data.
    std::vector<std::pair<std::string, flare::NoncontiguousBuffer>> files;
  };

  // Called upon completion of this task's command. This method is responsible
  // for prepare any "out-of-band" output, such as "extra-info", output files,
  // etc.
  //
  // On failure, status returned by this method overwrites exit-code / stderr,
  // and stdout is cleared.
  //
  // Not sure if we should provide a way for the implementation to mutate
  // `standard_output` / `standard_error`.
  virtual flare::Expected<OobOutput, flare::Status> GetOobOutput(
      int exit_code, const std::string& standard_output,
      const std::string& standard_error) = 0;

 private:
  void OnCompletion(int exit_code, flare::NoncontiguousBuffer standard_output,
                    flare::NoncontiguousBuffer standard_error) override;

 private:
  int exit_code_;
  std::string stdout_, stderr_;

  google::protobuf::Any extra_info_;
  flare::NoncontiguousBuffer file_pack_;
};

}  // namespace yadcc::daemon::cloud

#endif  // YADCC_DAEMON_CLOUD_REMOTE_TASK_H_
