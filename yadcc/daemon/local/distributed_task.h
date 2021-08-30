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

#ifndef YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_H_
#define YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "jsoncpp/value.h"

#include "flare/base/buffer.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/api/env_desc.pb.h"

namespace yadcc::daemon::local {

// @sa: `client/env_options.h`
enum class CacheControl {
  Disallow = 0,  // Don't touch cache.
  Allow = 1,     // Use existing one, or fill it on cache miss.
  Refill = 2  // Do not use the existing cache, but (re)fills it on completion.
};

// Describes output of a task.
struct DistributedTaskOutput {
  // Usually 0 means success. Negative code usually comes from RPC failure (not
  // an actual exit code.).
  int exit_code;

  // Output from `stdout` / `stderr`, respectively.
  std::string standard_output, standard_error;

  // Some task may use this field to pass extra task-specific information
  // between executor and requestor.
  google::protobuf::Any extra_info;

  // It's up to the individual task to determine K/V format. See
  // language-dependent task description for more details.
  std::vector<std::pair<std::string, flare::NoncontiguousBuffer>> output_files;
};

// Describes a distributed task.
class DistributedTask {
 public:
  virtual ~DistributedTask() = default;

  // Get process ID of the invoker. (A local process.)
  virtual pid_t GetInvokerPid() const = 0;

  // Get cache setting.
  //
  // The dispatcher use this to determine if the cache should be accessed.
  virtual CacheControl GetCacheSetting() const = 0;

  // Get cache key of this task.
  virtual std::string GetCacheKey() const = 0;

  // Task digest helps us in reducing unnecessary concurrent execution of
  // identical tasks.
  virtual std::string GetDigest() const = 0;

  // Get environment required by this task.
  //
  // The dispatcher uses it to grab a servant from scheduler.
  virtual const EnvironmentDesc& GetEnvironmentDesc() const = 0;

  // Dispatch this task at `stub`.
  virtual flare::Expected<std::uint64_t, flare::Status> StartTask(
      const std::string& token, std::uint64_t grant_id,
      cloud::DaemonService_SyncStub* stub) = 0;

  // Called upon task completion. You might want to save the arguments for later
  // use.
  virtual void OnCompletion(const DistributedTaskOutput& output) = 0;

  // Dump this task into a human readable format. For debugging purpose only.
  virtual Json::Value Dump() const = 0;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_DISTRIBUTED_TASK_H_
