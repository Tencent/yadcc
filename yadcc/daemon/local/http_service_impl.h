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

#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <tuple>

#include "flare/base/exposed_var.h"
#include "flare/rpc/http_handler.h"

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
  void SubmitTask(const flare::HttpRequest& request,
                  flare::HttpResponse* response,
                  flare::HttpServerContext* context);
  void WaitForTask(const flare::HttpRequest& request,
                   flare::HttpResponse* response,
                   flare::HttpServerContext* context);
  void AskToLeave(const flare::HttpRequest& request,
                  flare::HttpResponse* response,
                  flare::HttpServerContext* context);

 private:
  // Path, mtime, size.
  using CompilerPersonality =
      std::tuple<std::string, std::uint64_t, std::uint64_t>;

  // Try determine compiler digest from HTTP request. If the compiler is not
  // known to us yet, we cache the compiler digest.
  std::optional<std::string> TryGetOrSaveCompilerDigestByRequest(
      const flare::HttpRequest& request);

  Json::Value DumpCompilers();

 private:
  flare::ExposedVarDynamic<Json::Value> compiler_exposer_;

  // To support different version of compilers, we have to know compiler's
  // cryptographic digest. However, we can't afford to calculate it each time
  // our client is invoked, that would be rather slow.
  //
  // Therefore, to boost performance, the daemon caches compiler digest for each
  // compiler it has seen in this map. If a unseen-yet compiler appears, the
  // daemon asks the client to resubmit its request with compiler's digest, and
  // cache the result.
  //
  // This way, unless the daemon is restarted (relatively a rare event), we only
  // incur an extra `lstat` for each compiler invocation.
  //
  // Note that we can't calculate the digest of the compiler on behalf of our
  // caller. Since we can be running under a different account than our client,
  // it's possible that we don't have access to client's compiler at all.
  mutable std::shared_mutex compiler_digests_lock_;
  std::map<CompilerPersonality, std::string> compiler_digests_;
};

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_HTTP_SERVICE_IMPL_H_
