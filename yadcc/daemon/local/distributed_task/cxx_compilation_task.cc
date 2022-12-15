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

#include "yadcc/daemon/local/distributed_task/cxx_compilation_task.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "jsoncpp/json.h"

#include "flare/base/buffer.h"
#include "flare/base/string.h"
#include "flare/net/http/types.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/api/extra_info.pb.h"
#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/local/file_digest_cache.h"
#include "yadcc/daemon/task_digest.h"

using namespace std::literals;

namespace yadcc::daemon::local {

std::string CxxCompilationTask::GetCacheKey() const {
  return GetCxxCacheEntryKey(env_desc_, invocation_arguments_, source_digest_);
}

std::string CxxCompilationTask::GetDigest() const {
  return GetCxxTaskDigest(env_desc_, invocation_arguments_, source_digest_);
}

flare::Expected<std::uint64_t, flare::Status> CxxCompilationTask::StartTask(
    const std::string& token, std::uint64_t grant_id,
    cloud::DaemonService_SyncStub* stub) {
  cloud::QueueCxxCompilationTaskRequest req;
  req.set_token(token);

  req.set_task_grant_id(grant_id);
  *req.mutable_env_desc() = env_desc_;
  req.set_source_path(source_path_);
  req.set_invocation_arguments(invocation_arguments_);
  req.set_compression_algorithm(cloud::COMPRESSION_ALGORITHM_ZSTD);
  req.set_disallow_cache_fill(cache_control_ == CacheControl::Disallow);
  flare::RpcClientController ctlr;
  // This can take long if servant is in a DC that locates in a district
  // different than us.
  ctlr.SetTimeout(30s);
  ctlr.SetRequestAttachment(preprocessed_source_);
  // `preprocessed_source` can consume lots of memory, free it ASAP.
  preprocessed_source_.Clear();

  auto result = stub->QueueCxxCompilationTask(req, &ctlr);
  if (!result) {
    FLARE_LOG_WARNING("Rpc failed after {} seconds.",
                      ctlr.GetElapsedTime() / 1s);
    return result.error();
  }
  return result->task_id();
}

Json::Value CxxCompilationTask::Dump() const {
  Json::Value value;

  value["requestor_pid"] = static_cast<Json::UInt64>(requestor_pid_);
  value["cache_control"] = static_cast<int>(cache_control_);
  value["source_digest"] = source_digest_;
  value["compiler_digest"] = env_desc_.compiler_digest();
  value["source_path"] = source_path_;
  value["invocation_arguments"] = invocation_arguments_;
  return value;
}

flare::Status CxxCompilationTask::Prepare(
    const SubmitCxxTaskRequest& request,
    const std::vector<flare::NoncontiguousBuffer>& bytes) {
  // Arguments validation.
  if (request.requestor_process_id() <= 1 || request.source_path().empty() ||
      request.compiler_invocation_arguments().empty() ||
      (request.cache_control() < 0 || request.cache_control() > 2) ||
      (request.cache_control() != 0 && request.source_digest().empty())) {
    return flare::Status(flare::underlying_value(flare::HttpStatus::BadRequest),
                         "Invalid arguments.");
  }

  auto file_digest = FileDigestCache::Instance()->TryGet(
      request.compiler().path(), request.compiler().size(),
      request.compiler().timestamp());
  if (!file_digest) {
    return flare::Status(flare::underlying_value(flare::HttpStatus::BadRequest),
                         "Compiler digest is unknown.");
  }
  env_desc_.set_compiler_digest(*file_digest);
  // Requestor.
  requestor_pid_ = request.requestor_process_id();
  // Source information.
  source_path_ = request.source_path();
  source_digest_ = request.source_digest();
  invocation_arguments_ = request.compiler_invocation_arguments();
  // Controls how the compilation should be performed.
  cache_control_ = static_cast<CacheControl>(request.cache_control());
  preprocessed_source_ = std::move(bytes[0]);

  return flare::Status(0);
}

flare::Expected<CxxCompilationTask::Output, flare::Status>
CxxCompilationTask::RebuildOutput(const DistributedTaskOutput& output) {
  WaitForCxxTaskResponse resp_msg;
  resp_msg.set_exit_code(output.exit_code);
  resp_msg.set_output(output.standard_output);
  resp_msg.set_error(output.standard_error);

  std::vector<flare::NoncontiguousBuffer> buffers;
  if (output.exit_code < 0) {  // RPC failure.
    return std::pair(resp_msg, buffers);
  }

  CxxCompilationExtraInfo comp_info;
  // `comp_info` presents only if the compilation succeeded.
  if (output.exit_code == 0 && !output.extra_info.UnpackTo(&comp_info)) {
    return flare::Status{
        flare::underlying_value(flare::HttpStatus::InternalServerError),
        flare::Format("Unexpected: Malformed C++ compilation info. Got message "
                      "of type [{}].",
                      output.extra_info.type_url())};
  }

  for (auto&& e : output.output_files) {
    resp_msg.add_file_extensions(e.first);
    *resp_msg.add_patches() = (*comp_info.mutable_file_name_patches())[e.first];
    buffers.push_back(e.second);
  }

  return std::pair(resp_msg, buffers);
}

}  // namespace yadcc::daemon::local
