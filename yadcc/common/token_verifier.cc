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
#include <unordered_set>
#include <utility>

#include "gflags/gflags.h"

#include "flare/base/logging.h"
#include "flare/base/string.h"

DEFINE_string(acceptable_user_tokens, "",
              "List of acceptable client tokens. This option is not always "
              "respected. The client program may specify its own list of "
              "tokens from other sources.");
DEFINE_string(acceptable_servant_tokens, "",
              "List of acceptable servant tokens.");

namespace yadcc {

TokenVerifier::TokenVerifier(std::unordered_set<std::string> recognized_tokens)
    : recognized_tokens_(std::move(recognized_tokens)) {
  FLARE_LOG_WARNING_IF_ONCE(
      recognized_tokens_.count(""),
      "POSSIBLE SECURITY BREACH. Empty token is allowed. This effectively "
      "disables token verification. Unless you're allowing guest users to "
      "access your service, this is likely a misconfiguration.");

  // Or `CHECK`?
  FLARE_LOG_ERROR_IF_ONCE(
      recognized_tokens_.empty(),
      "You should provide at least one recognized tokens, otherwise no one "
      "would be able to access your service.");
}

bool TokenVerifier::Verify(const std::string& token) const noexcept {
  // `.contains` is not implemented by libstdc++ 8.2 (what we're using.).
  return recognized_tokens_.count(token) != 0;
}

std::unique_ptr<TokenVerifier> MakeTokenVerifierFromFlag(
    const std::string& flags) {
  FLARE_CHECK(!flags.empty(),
              "You should provide at least one recognized token.");

  auto tokens = flare::Split(flags, ",", true /* keep_empty */);

  // ... Well dealing with `std::string_view` is hard.
  std::unordered_set<std::string> translated;
  for (auto&& e : tokens) {
    translated.insert(std::string(e));
  }
  return std::make_unique<TokenVerifier>(translated);
}

}  // namespace yadcc
