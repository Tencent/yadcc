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

#include "yadcc/common/parse_size.h"

#include <optional>
#include <string>

#include "flare/base/logging.h"
#include "flare/base/string.h"

namespace yadcc {

std::optional<std::size_t> TryParseSize(const std::string& size_str) {
  std::string_view size_str_view(size_str);
  std::uint64_t scale = 1;
  if (size_str_view.back() == 'G') {
    scale = 1 << 30;
    size_str_view.remove_suffix(1);
  } else if (size_str_view.back() == 'M') {
    scale = 1 << 20;
    size_str_view.remove_suffix(1);
  } else if (size_str_view.back() == 'K') {
    scale = 1 << 10;
    size_str_view.remove_suffix(1);
  } else if (size_str_view.back() == 'B') {
    size_str_view.remove_suffix(1);
  }
  auto size = flare::TryParse<std::size_t>(size_str_view);
  if (size == std::nullopt) {
    return {};
  }
  return (*size) * scale;
}

}  // namespace yadcc
