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

#include <string>

#include "gflags/gflags.h"
#include "gtest/gtest.h"

#include "flare/base/encoding.h"
#include "flare/base/string.h"
#include "flare/net/http/http_request.h"
#include "flare/net/http/http_response.h"

using namespace std::literals;

DECLARE_string(inspect_credential);

namespace yadcc {

std::string EncodeAuth(const std::string& user, const std::string& password) {
  return flare::Format(
      "Basic {}", flare::EncodeBase64(flare::Format("{}:{}", user, password)));
}

TEST(InspectAuth, All) {
  FLAGS_inspect_credential = "Alice:with_her_password";

  auto filter = MakeInspectAuthFilter();
  flare::HttpRequest req;
  flare::HttpResponse resp;
  flare::HttpServerContext context;
  req.set_uri("/inspect/gflags");
  req.set_method(flare::HttpMethod::Post);

  // No credential provided.
  EXPECT_EQ(flare::HttpFilter::Action::EarlyReturn,
            filter->OnFilter(&req, &resp, &context));

  // Unacceptable credential provided.
  req.headers()->Set("Authorization",
                     EncodeAuth("Bob", "and_his_boring_password"));
  EXPECT_EQ(flare::HttpFilter::Action::EarlyReturn,
            filter->OnFilter(&req, &resp, &context));

  // Correct credential.
  req.headers()->Set("Authorization", EncodeAuth("Alice", "with_her_password"));
  EXPECT_EQ(flare::HttpFilter::Action::KeepProcessing,
            filter->OnFilter(&req, &resp, &context));
}

}  // namespace yadcc
