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

#ifndef YADCC_DAEMON_LOCAL_HTTP_SERVICE_IMPL_H_
#define YADCC_DAEMON_LOCAL_HTTP_SERVICE_IMPL_H_

#include <optional>
#include <string>

#include "flare/rpc/http_handler.h"

#include "yadcc/daemon/local/distributed_task_dispatcher.h"

namespace yadcc::daemon::local {

// This handler handles requests from local compiler wrapper.
class HttpServiceImpl : public flare::HttpHandler {
 public:
  HttpServiceImpl();

  void OnGet(const flare::HttpRequest& request, flare::HttpResponse* response,
             flare::HttpServerContext* context) override;

  void OnPost(const flare::HttpRequest& request, flare::HttpResponse* response,
              flare::HttpServerContext* context) override;

 private:
  void GetVersion(const flare::HttpRequest& request,
                  flare::HttpResponse* response,
                  flare::HttpServerContext* context);
  void AcquireQuota(const flare::HttpRequest& request,
                    flare::HttpResponse* response,
                    flare::HttpServerContext* context);
  void ReleaseQuota(const flare::HttpRequest& request,
                    flare::HttpResponse* response,
                    flare::HttpServerContext* context);
  void SetFileDigest(const flare::HttpRequest& request,
                     flare::HttpResponse* response,
                     flare::HttpServerContext* context);
  void SubmitCxxTask(const flare::HttpRequest& request,
                     flare::HttpResponse* response,
                     flare::HttpServerContext* context);
  template <class Request, class Task>
  void WaitForTaskGeneric(const flare::HttpRequest& request,
                          flare::HttpResponse* response,
                          flare::HttpServerContext* context);
  void AskToLeave(const flare::HttpRequest& request,
                  flare::HttpResponse* response,
                  flare::HttpServerContext* context);
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_HTTP_SERVICE_IMPL_H_
