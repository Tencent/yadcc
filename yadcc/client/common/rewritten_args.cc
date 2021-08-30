// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "yadcc/client/common/rewritten_args.h"

#include "yadcc/client/common/escape.h"

namespace yadcc::client {

std::string RewrittenArgs::ToCommandLine(bool with_program) const {
  std::string result;
  if (with_program) {
    result += program_ + " ";
  }
  for (auto&& e : args_) {
    result += EscapeCommandArgument(e) + " ";
  }
  if (!result.empty()) {
    result.pop_back();
  }
  return result;
}

std::string RewrittenArgs::ToString() const {
  std::string result;
  result += program_;
  for (auto&& e : args_) {
    result += " " + e;
  }
  return result;
}

}  // namespace yadcc::client
