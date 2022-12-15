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

#include "yadcc/common/inspect_auth.h"

#include <string_view>

#include "gflags/gflags.h"

#include "flare/base/string.h"
#include "flare/rpc/builtin/basic_authentication_http_filter.h"

DEFINE_string(
    inspect_credential, "",
    "HTTP basic-auth credential for accessing `/inspect`. This option should "
    "be specified as 'user:password'. Leaving it empty disables inspection "
    "interfaces unless `debugging_no_inspect_auth` is set.");
DEFINE_bool(debugging_no_inspect_auth, false,
            "If set, `/inspect` is freely accessible. This can be a security "
            "breach. This flag should only be used for debugging purpose.");

namespace yadcc {

namespace {

bool IsCredentialAcceptable(const std::string_view& user,
                            const std::string_view& password) {
  if (FLAGS_inspect_credential.empty()) {
    return false;
  }
  return flare::Format("{}:{}", user, password) == FLAGS_inspect_credential;
}

class NoopFilter : public flare::HttpFilter {
 public:
  Action OnFilter(flare::HttpRequest* request, flare::HttpResponse* response,
                  flare::HttpServerContext* context) {
    return Action::KeepProcessing;
  }
};

}  // namespace

std::unique_ptr<flare::HttpFilter> MakeInspectAuthFilter() {
  if (FLAGS_debugging_no_inspect_auth) {
    return std::make_unique<NoopFilter>();
  }
  return std::make_unique<flare::BasicAuthenticationHttpFilter>(
      IsCredentialAcceptable, "/inspect");
}

}  // namespace yadcc
