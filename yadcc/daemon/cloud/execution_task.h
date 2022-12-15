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

#ifndef YADCC_DAEMON_CLOUD_EXECUTION_TASK_H_
#define YADCC_DAEMON_CLOUD_EXECUTION_TASK_H_

#include <optional>
#include <string>

#include "jsoncpp/value.h"

#include "flare/base/buffer.h"
#include "flare/base/ref_ptr.h"

namespace yadcc::daemon::cloud {

// This interface defines a abstract task interface for `ExecutionEngine` to
// work with.
class ExecutionTask : public flare::RefCounted<ExecutionTask> {
 public:
  virtual ~ExecutionTask() = default;

  // Returns command line to execute.
  virtual std::string GetCommandLine() const = 0;

  // Returns bytes to be fed to the command.
  //
  // To keep memory footprint low, the `ExecutionEngine` guarantees that this
  // method is called ONLY ONCE. This allows you to move standard input to the
  // return value, hoping for the engine to free it earlier.
  virtual flare::NoncontiguousBuffer GetStandardInputOnce() = 0;

  // Called upon command (@sa: `GetCommandLine()`) completes. The task can do
  // necessary post-execution processing here (such as cleanup, filling cache,
  // etc.).
  //
  // The implementation may save the arguments given as necessary.
  virtual void OnCompletion(int exit_code,
                            flare::NoncontiguousBuffer standard_output,
                            flare::NoncontiguousBuffer standard_error) = 0;

  // Dumps internals about this task.
  //
  // For debugging purpose.
  virtual Json::Value DumpInternals() const = 0;
};

}  // namespace yadcc::daemon::cloud

#endif  // YADCC_DAEMON_CLOUD_EXECUTION_TASK_H_
