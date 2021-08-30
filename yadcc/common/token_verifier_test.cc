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

#include <string>

#include "gflags/gflags.h"
#include "gtest/gtest.h"

using namespace std::literals;

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
  std::string test_tokens = "token1,token2";
  auto verifier = MakeTokenVerifierFromFlag(test_tokens);
  EXPECT_TRUE(verifier->Verify("token1"));
  EXPECT_TRUE(verifier->Verify("token2"));
  EXPECT_FALSE(verifier->Verify("token3"));
  EXPECT_FALSE(verifier->Verify(""));
}

TEST(TokenVerifier, FromGFlags2) {
  std::string test_tokens = "token1,token2,";  // Empty token is allowed.
  auto verifier = MakeTokenVerifierFromFlag(test_tokens);
  EXPECT_TRUE(verifier->Verify("token1"));
  EXPECT_TRUE(verifier->Verify("token2"));
  EXPECT_FALSE(verifier->Verify("token3"));
  EXPECT_TRUE(verifier->Verify(""));
}

}  // namespace yadcc
