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

#include "yadcc/common/token_verifier.h"

#include "thirdparty/gflags/gflags.h"
#include "thirdparty/googletest/gtest/gtest.h"

using namespace std::literals;

DECLARE_string(acceptable_tokens);

namespace yadcc {

TEST(TokenVerifier, Normal) {
  TokenVerifier verifier({"my", "fancy", "token"});

  EXPECT_TRUE(verifier.Verify("my"));
  EXPECT_TRUE(verifier.Verify("fancy"));
  EXPECT_TRUE(verifier.Verify("token"));
  EXPECT_FALSE(verifier.Verify("but"));
  EXPECT_FALSE(verifier.Verify("not"));
  EXPECT_FALSE(verifier.Verify("this"));
}

TEST(TokenVerifier, FromGFlags) {
  FLAGS_acceptable_tokens = "token1,token2";
  auto verifier = MakeTokenVerifierFromFlag();
  EXPECT_TRUE(verifier->Verify("token1"));
  EXPECT_TRUE(verifier->Verify("token2"));
  EXPECT_FALSE(verifier->Verify("token3"));
  EXPECT_FALSE(verifier->Verify(""));
}

TEST(TokenVerifier, FromGFlags2) {
  FLAGS_acceptable_tokens = "token1,token2,";  // Empty token is allowed.
  auto verifier = MakeTokenVerifierFromFlag();
  EXPECT_TRUE(verifier->Verify("token1"));
  EXPECT_TRUE(verifier->Verify("token2"));
  EXPECT_FALSE(verifier->Verify("token3"));
  EXPECT_TRUE(verifier->Verify(""));
}

}  // namespace yadcc
