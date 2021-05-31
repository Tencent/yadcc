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

#include "yadcc/daemon/local/http_service_impl.h"

#include <signal.h>
#include <sys/types.h>

#include <string>

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/jsoncpp/json.h"

#include "flare/base/buffer.h"
#include "flare/base/encoding.h"
#include "flare/base/function.h"
#include "flare/net/http/http_request.h"
#include "flare/net/http/http_response.h"
#include "flare/rpc/rpc_channel.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/common_flags.h"
#include "yadcc/daemon/local/distributed_task_dispatcher.h"
#include "yadcc/daemon/local/local_task_monitor.h"

using namespace std::literals;

// Provided by blade.
extern "C" {

namespace binary_version {

extern const char kBuildTime[];

}  // namespace binary_version
}

namespace yadcc::daemon::local {

HttpServiceImpl::HttpServiceImpl()
    : compiler_exposer_("yadcc/compilers", [this] { return DumpCompilers(); }) {
}

// This is rather ugly. TODO(luobogao): Let's see if we can provide the same
// functionality in Flare.
using Handler = void (HttpServiceImpl::*)(const flare::HttpRequest&,
                                          flare::HttpResponse*,
                                          flare::HttpServerContext*);

void HttpServiceImpl::OnGet(const flare::HttpRequest& request,
                            flare::HttpResponse* response,
                            flare::HttpServerContext* context) {
  static const std::unordered_map<std::string, Handler> kHandlers = {
      {"/local/get_version", &HttpServiceImpl::GetVersion}};

  if (auto iter = kHandlers.find(request.uri()); iter != kHandlers.end()) {
    return (this->*(iter->second))(request, response, context);
  }

  // Not recognized method then.
  flare::GenerateDefaultResponsePage(flare::HttpStatus::NotFound, response);
}

void HttpServiceImpl::OnPost(const flare::HttpRequest& request,
                             flare::HttpResponse* response,
                             flare::HttpServerContext* context) {
  static const std::unordered_map<std::string, Handler> kHandlers = {
      {"/local/acquire_quota", &HttpServiceImpl::AcquireQuota},
      {"/local/release_quota", &HttpServiceImpl::ReleaseQuota},
      {"/local/submit_task", &HttpServiceImpl::SubmitTask},
      {"/local/wait_for_task", &HttpServiceImpl::WaitForTask},
      {"/local/ask_to_leave", &HttpServiceImpl::AskToLeave}};

  if (auto iter = kHandlers.find(request.uri()); iter != kHandlers.end()) {
    return (this->*(iter->second))(request, response, context);
  }

  // Not recognized method then.
  flare::GenerateDefaultResponsePage(flare::HttpStatus::NotFound, response);
}

void HttpServiceImpl::GetVersion(const flare::HttpRequest& request,
                                 flare::HttpResponse* response,
                                 flare::HttpServerContext* context) {
  Json::Value jsv;
  jsv["built_at"] = binary_version::kBuildTime;
  jsv["version_for_upgrade"] = version_for_upgrade;
  response->set_body(Json::FastWriter().write(jsv));
}

void HttpServiceImpl::AcquireQuota(const flare::HttpRequest& request,
                                   flare::HttpResponse* response,
                                   flare::HttpServerContext* context) {
  Json::Value jsv;
  if (!Json::Reader().parse(*request.body(), jsv) || !jsv.isObject() ||
      !jsv["milliseconds_to_wait"].isUInt() ||
      !jsv["lightweight_task"].isBool() || !jsv["requestor_pid"].isUInt()) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Invalid arguments.");
    return;
  }

  constexpr auto kMaximumWaitableTime = 10s;
  auto desired_wait = jsv["milliseconds_to_wait"].asUInt() * 1ms;

  // Well I don't check validity of these arguments. Providing faking arguments
  // only hurts the requestor himself.
  auto lightweight_task = jsv["lightweight_task"].asBool();
  auto requestor_pid = jsv["requestor_pid"].asUInt();

  if (!LocalTaskMonitor::Instance()->WaitForRunningNewTaskPermission(
          requestor_pid, lightweight_task, desired_wait)) {
    response->set_status(flare::HttpStatus::ServiceUnavailable);
  }
  // `response` is left as a successful one.
}

void HttpServiceImpl::ReleaseQuota(const flare::HttpRequest& request,
                                   flare::HttpResponse* response,
                                   flare::HttpServerContext* context) {
  Json::Value jsv;
  if (!Json::Reader().parse(*request.body(), jsv) || !jsv.isObject() ||
      !jsv["requestor_pid"].isUInt()) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Invalid arguments.");
    return;
  }

  // This can't fail.
  LocalTaskMonitor::Instance()->DropTaskPermission(
      jsv["requestor_pid"].asUInt());
}

void HttpServiceImpl::SubmitTask(const flare::HttpRequest& request,
                                 flare::HttpResponse* response,
                                 flare::HttpServerContext* context) {
  // Requestor.
  auto requestor_pid =
      request.headers()->TryGet<int>("X-Requestor-Process-Id").value_or(-1);
  // Source information.
  std::string source_path{
      // FIXME: Do we need to pct-encode the path?
      request.headers()->TryGet("X-Source-Path").value_or("")};
  std::string source_digest{
      request.headers()->TryGet("X-Source-Digest").value_or("")};
  // Controls how the compilation should be performed.
  std::string invocation_args{request.headers()
                                  ->TryGet("X-Compiler-Invocation-Arguments")
                                  .value_or("")};
  auto cache_control =
      request.headers()->TryGet<int>("X-Cache-Control").value_or(1);

  // Arguments validation.
  if (requestor_pid <= 1 || source_path.empty() || invocation_args.empty() ||
      (cache_control < 0 || cache_control > 2) ||
      (cache_control != 0 && source_digest.empty()) ||
      request.headers()->TryGet("Content-Encoding") != "application/zstd") {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Invalid arguments.");
    return;
  }
  auto compiler_digest = TryGetOrSaveCompilerDigestByRequest(request);
  if (!compiler_digest) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Invalid arguments.");
    return;
  }

  // TODO(luobogao): If there's no process whose PID is `requestor_pid`, bail
  // out.

  // Queue this task to the dispatcher.
  CompilationTask task;
  task.requestor_pid = requestor_pid;
  task.env_desc.set_compiler_digest(*compiler_digest);
  task.source_digest = source_digest;
  task.cache_control = static_cast<CacheControl>(cache_control);
  task.source_path = source_path;
  task.invocation_arguments = invocation_args;
  task.preprocessed_source = std::move(*request.noncontiguous_body());

  // If the task can't be dispatched after 5min, bail out.
  auto task_id = DistributedTaskDispatcher::Instance()->QueueTask(
      std::move(task), flare::ReadCoarseSteadyClock() + 5min);
  // Fill the response.
  response->set_body(fmt::format("{{\"task_id\":\"{}\"}}", task_id));
}

void HttpServiceImpl::WaitForTask(const flare::HttpRequest& request,
                                  flare::HttpResponse* response,
                                  flare::HttpServerContext* context) {
  Json::Value req_jsv;
  if (!Json::Reader().parse(*request.body(), req_jsv) || !req_jsv.isObject() ||
      !req_jsv["milliseconds_to_wait"].isUInt() ||
      !req_jsv["task_id"].isString()) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Invalid arguments.");
    return;
  }
  auto task_id = flare::TryParse<std::uint64_t>(req_jsv["task_id"].asString());
  if (!task_id) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Invalid arguments.");
    return;
  }

  constexpr auto kMaximumWaitableTime = 10s;
  auto desired_wait = req_jsv["milliseconds_to_wait"].asUInt() * 1ms;

  // Sanity checks.
  if (desired_wait > kMaximumWaitableTime) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Unacceptable `milliseconds_to_wait`.");
    return;
  }

  CompilationOutput output;
  auto status = DistributedTaskDispatcher::Instance()->WaitForTask(
      *task_id, desired_wait, &output);
  if (status == DistributedTaskDispatcher::WaitStatus::Timeout) {
    response->set_status(flare::HttpStatus::ServiceUnavailable);
    return;
  } else if (status == DistributedTaskDispatcher::WaitStatus::NotFound) {
    FLARE_LOG_WARNING_EVERY_SECOND(
        "Received a request for a non-existing task ID [{}].", *task_id);
    response->set_status(flare::HttpStatus::NotFound);
    return;
  }

  // Fill the response.
  //
  // The format of the response is a bit messy. I can't come up with a better
  // format for the moment. (No, simply storing `stdout` / `stderr` in HTTP
  // header won't work, as `stderr` can be rather large.)
  //
  // Perhaps we should implement something like `WriteDelimited` for
  // `NoncontiguousBuffer` in Flare? That would allow us to efficiently
  // concatenate multiple buffers together.
  flare::NoncontiguousBufferBuilder resp_builder;
  Json::Value jsv;
  jsv["exit_code"] = output.exit_code;
  jsv["output"] = output.standard_output;
  jsv["error"] = output.standard_error;
  auto first_part = Json::FastWriter().write(jsv);
  resp_builder.Append(fmt::format("{:x}\r\n{}\r\n{:x}\r\n", first_part.size(),
                                  first_part, output.object_file.ByteSize()));
  resp_builder.Append(output.object_file);
  resp_builder.Append("\r\n");
  response->set_body(resp_builder.DestructiveGet());
}

void HttpServiceImpl::AskToLeave(const flare::HttpRequest& request,
                                 flare::HttpResponse* response,
                                 flare::HttpServerContext* context) {
  FLARE_LOG_INFO("Someone asked us to leave. Killing ourselves.");
  kill(getpid(), SIGINT);
}

std::optional<std::string> HttpServiceImpl::TryGetOrSaveCompilerDigestByRequest(
    const flare::HttpRequest& request) {
  auto path = request.headers()->TryGet("X-Compiler-Path");
  auto mtime =
      request.headers()->TryGet<std::uint64_t>("X-Compiler-Modification-Time");
  auto size = request.headers()->TryGet<std::uint64_t>("X-Compiler-Size");

  // For backward compatibility, we check `X-Compiler-Digest` first.
  auto compiler_digest = request.headers()->TryGet("X-Compiler-Digest");
  if (compiler_digest && compiler_digest->empty()) {
    return std::nullopt;  // Invalid argument, to be precise.
  }

  // The old protocol.
  if (!path || !mtime || !size) {
    // Compiler digest must be provided by the client if the old protocol is
    // used.
    if (!compiler_digest) {
      return std::nullopt;
    }
    return std::string(*compiler_digest);
  }

  // Newer protocol then.
  if (path->empty() || path->front() != '/' /* Absolute path is required. */ ||
      !*mtime || !*size) {
    return std::nullopt;  // Invalid argument, to be precise.
  }

  std::tuple personality(std::string(*path), *mtime, *size);
  if (compiler_digest) {
    // If the caller is kind enough to provide us with the compiler's digest, we
    // cache it.
    //
    // This allows the client not to calculate the digest (which can be costly)
    // of compiler each time a file is compiled. Instead, the client can provide
    // only `lstat` info of the compiler in the subsequent compilation requests.
    std::scoped_lock _(compiler_digests_lock_);
    compiler_digests_[personality] = *compiler_digest;
    return std::string(*compiler_digest);
  } else {
    // The client provides only the basic information of the compiler, but not
    // the digest. Let's see if the digest is known to us.
    std::shared_lock _(compiler_digests_lock_);
    if (auto iter = compiler_digests_.find(personality);
        iter != compiler_digests_.end()) {
      return iter->second;
    }
    return std::nullopt;  // Unrecognized compiler.
  }
}

Json::Value HttpServiceImpl::DumpCompilers() {
  Json::Value jsv;
  std::shared_lock _(compiler_digests_lock_);
  for (auto&& [k, v] : compiler_digests_) {
    auto&& [path, mtime, size] = k;
    auto&& entry = jsv[path];
    entry["mtime"] = static_cast<Json::UInt64>(mtime);
    entry["size"] = static_cast<Json::UInt64>(size);
    entry["digest"] = v;
  }
  return jsv;
}

}  // namespace yadcc::daemon::local
