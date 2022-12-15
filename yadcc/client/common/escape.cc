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

#include "yadcc/client/common/escape.h"

#include <string_view>

namespace yadcc::client {

// Shamelessly copied from common/encoding/shell.*: `ShellEscape`.
std::string EscapeCommandArgument(const std::string_view& str) {
  std::string result;
  for (size_t i = 0; i < str.size(); ++i) {
    switch (str[i]) {
      case '\a':
        result += "\\a";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\v':
        result += "\\v";
        break;
      case ' ':
      case '>':
      case '<':
      case '!':
      case '"':
      case '#':
      case '$':
      case '&':
      case '(':
      case ')':
      case '*':
      case ',':
      case ':':
      case ';':
      case '?':
      case '@':
      case '[':
      case '\\':
      case ']':
      case '`':
      case '{':
      case '}':
        result += '\\';
        [[fallthrough]];
      default:
        result += str[i];
        break;
    }
  }
  return result;
}

}  // namespace yadcc::client
