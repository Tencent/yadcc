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

#ifndef YADCC_COMMON_TOKEN_VERIFIER_H_
#define YADCC_COMMON_TOKEN_VERIFIER_H_

#include <memory>
#include <string>
#include <unordered_set>

namespace yadcc {

// This class helps us to verify user's token.
//
// Well the token is verified in a rather simple way (string comparison).
class TokenVerifier {
 public:
  // Default initialized one allow no token.
  TokenVerifier() = default;

  // Initialize a verifier that recognizes tokens listed in `recognized_tokens`.
  //
  // CAUTION: Empty token is allowed by the implementation. Unless you're
  // allowing "guest" users to access your service, this is likely a security
  // breach.
  explicit TokenVerifier(std::unordered_set<std::string> recognized_tokens);

  // Check if `token` is recognized.
  bool Verify(const std::string& token) const noexcept;

 private:
  std::unordered_set<std::string> recognized_tokens_;
};

// Make a `TokenVerifier` accepting token listed in flag `acceptable_tokens`.
//
// Returning `std::unique_ptr` (instead of an object) for coding simplicity.
std::unique_ptr<TokenVerifier> MakeTokenVerifierFromFlag();

}  // namespace yadcc

#endif  // YADCC_COMMON_TOKEN_VERIFIER_H_
