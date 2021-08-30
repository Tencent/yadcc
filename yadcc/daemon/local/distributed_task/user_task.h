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

#ifndef YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_USER_TASK_H_
#define YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_USER_TASK_H_

#include <utility>
#include <vector>

#include "flare/base/expected.h"
#include "flare/base/status.h"

#include "yadcc/daemon/local/distributed_task.h"

namespace yadcc::daemon::local {

// Helps implementing tasks whose output is a message of `T` and a collection of
// attachments.
template <class T>
class UserTask : public DistributedTask {
 public:
  using Output = std::pair<T, std::vector<flare::NoncontiguousBuffer>>;

  // Everything declared by `DistributedTask` other than `OnCompletion` should
  // be implemented by subclass of this class.

  // Get output to sent back to the client.
  flare::Expected<Output, flare::Status> GetOutput() const { return output_; }

 protected:
  // Generates a response or an error to the client.
  //
  // Note that error code should be HTTP status code.
  virtual flare::Expected<Output, flare::Status> RebuildOutput(
      const DistributedTaskOutput& output) = 0;

 private:
  void OnCompletion(const DistributedTaskOutput& output) override {
    output_ = RebuildOutput(std::move(output));
  }

 private:
  flare::Expected<Output, flare::Status> output_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_USER_TASK_H_
