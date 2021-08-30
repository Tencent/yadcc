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

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gflags/gflags.h"
#include "jsoncpp/json.h"

#include "flare/base/buffer.h"
#include "flare/base/encoding.h"
#include "flare/base/expected.h"
#include "flare/base/function.h"
#include "flare/net/http/http_request.h"
#include "flare/net/http/http_response.h"
#include "flare/rpc/rpc_channel.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/common_flags.h"
#include "yadcc/daemon/local/distributed_task/cxx_compilation_task.h"
#include "yadcc/daemon/local/distributed_task_dispatcher.h"
#include "yadcc/daemon/local/file_digest_cache.h"
#include "yadcc/daemon/local/local_task_monitor.h"
#include "yadcc/daemon/local/messages.pb.h"
#include "yadcc/daemon/local/packing.h"

using namespace std::literals;

// Provided by blade.
extern "C" {

namespace binary_version {

extern const char kBuildTime[];

}  // namespace binary_version
}

namespace yadcc::daemon::local {

HttpServiceImpl::HttpServiceImpl() {}

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
      {"/local/set_file_digest", &HttpServiceImpl::SetFileDigest},
      {"/local/submit_cxx_task", &HttpServiceImpl::SubmitCxxTask},
      {"/local/wait_for_cxx_task",
       &HttpServiceImpl::WaitForTaskGeneric<WaitForCxxTaskRequest,
                                            CxxCompilationTask>},
      {"/local/ask_to_leave", &HttpServiceImpl::AskToLeave}};

  FLARE_VLOG(1, "Calling [{}].", request.uri());
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

void HttpServiceImpl::SetFileDigest(const flare::HttpRequest& request,
                                    flare::HttpResponse* response,
                                    flare::HttpServerContext* context) {
  auto req = TryParseJsonAsMessage<SetFileDigestRequest>(*request.body());
  if (!req) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body(
        flare::Format("Failed to parse request: {}", req.error().ToString()));
    return;
  }
  FileDigestCache::Instance()->Set(req->file_desc().path(),
                                   req->file_desc().size(),
                                   req->file_desc().timestamp(), req->digest());
}

void HttpServiceImpl::SubmitCxxTask(const flare::HttpRequest& request,
                                    flare::HttpResponse* response,
                                    flare::HttpServerContext* context) {
  auto parsed_opt = TryParseMultiChunkRequest<SubmitCxxTaskRequest>(
      *request.noncontiguous_body());
  if (!parsed_opt) {
    response->set_status(
        static_cast<flare::HttpStatus>(parsed_opt.error().code()));
    response->set_body(parsed_opt.error().message());
    return;
  }

  auto task = std::make_unique<CxxCompilationTask>();
  if (auto status = task->Prepare(parsed_opt->first, parsed_opt->second);
      !status.ok()) {
    response->set_status(static_cast<flare::HttpStatus>(status.code()));
    response->set_body(status.message());
    return;
  }

  SubmitCxxTaskResponse resp_msg;
  resp_msg.set_task_id(DistributedTaskDispatcher::Instance()->QueueTask(
      std::move(task), flare::ReadCoarseSteadyClock() + 5min));
  response->set_body(WriteMessageAsJson(resp_msg));
}

template <class Request, class Task>
void HttpServiceImpl::WaitForTaskGeneric(const flare::HttpRequest& request,
                                         flare::HttpResponse* response,
                                         flare::HttpServerContext* context) {
  constexpr auto kMaximumWaitableTime = 10s;

  auto req_msg = TryParseJsonAsMessage<Request>(*request.body());
  if (!req_msg) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body(flare::Format("Failed to parse request: {}",
                                     req_msg.error().ToString()));
    return;
  }
  if (req_msg->milliseconds_to_wait() * 1ms > kMaximumWaitableTime) {
    response->set_status(flare::HttpStatus::BadRequest);
    response->set_body("Unacceptable `milliseconds_to_wait`.");
    return;
  }

  auto wait_result = DistributedTaskDispatcher::Instance()->WaitForTask<Task>(
      req_msg->task_id(), req_msg->milliseconds_to_wait() * 1ms);
  if (!wait_result) {
    if (wait_result.error() == DistributedTaskDispatcher::WaitStatus::Timeout) {
      response->set_status(flare::HttpStatus::ServiceUnavailable);
      return;
    } else if (wait_result.error() ==
               DistributedTaskDispatcher::WaitStatus::NotFound) {
      FLARE_LOG_WARNING_EVERY_SECOND(
          "Received a request for a non-existing task ID [{}].",
          req_msg->task_id());
      response->set_status(flare::HttpStatus::NotFound);
      return;
    }
  }

  if (auto output = wait_result->get()->GetOutput()) {
    response->set_body(WriteMultiChunkResponse(output->first, output->second));
  } else {
    response->set_status(static_cast<flare::HttpStatus>(output.error().code()));
    response->set_body(output.error().message());
  }
}

void HttpServiceImpl::AskToLeave(const flare::HttpRequest& request,
                                 flare::HttpResponse* response,
                                 flare::HttpServerContext* context) {
  FLARE_LOG_INFO("Someone asked us to leave. Killing ourselves.");
  kill(getpid(), SIGINT);
}

}  // namespace yadcc::daemon::local
