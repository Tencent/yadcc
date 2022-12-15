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

#include "yadcc/client/common/multi_chunk.h"

#include <climits>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yadcc/client/common/utility.h"

namespace yadcc::client {

std::string MakeMultiChunkHeader(const std::vector<std::string_view>& parts) {
  if (parts.empty()) {
    return {};
  }

  std::string result;
  for (auto&& e : parts) {
    result += std::to_string(e.size()) + ",";
  }
  result.pop_back();
  result += "\r\n";
  return result;
}

std::string MakeMultiChunk(const std::vector<std::string_view>& parts) {
  if (parts.empty()) {
    return {};
  }

  std::size_t total_size = 0;
  std::string result;
  for (auto&& e : parts) {
    result += std::to_string(e.size()) + ",";
    total_size += e.size();
  }
  result.pop_back();
  result += "\r\n";
  result.reserve(result.size() + total_size);
  for (auto&& e : parts) {
    result.append(e);
  }
  return result;
}

std::optional<std::vector<std::string_view>> TryParseMultiChunk(
    const std::string_view& view) {
  std::vector<std::string_view> result;
  if (view.empty()) {
    return result;
  }

  auto delim = view.find('\n');
  if (delim == std::string_view::npos || delim == 0 ||
      view[delim - 1] != '\r') {
    return std::nullopt;
  }

  auto sizes = Split(view.substr(0, delim - 1), ",");
  auto rest_bytes = view.substr(delim + 1);
  std::vector<std::size_t> parsed_size;
  std::size_t total_size = 0;
  for (auto&& e : sizes) {
    auto str = std::string(e);
    auto size = strtol(str.data(), nullptr, 10);
    if (size <= 0 || size == LONG_MAX || size == LONG_MIN) {
      return std::nullopt;
    }
    parsed_size.push_back(size);
    total_size += size;
  }

  if (total_size != rest_bytes.size()) {
    return std::nullopt;
  }
  for (auto&& e : parsed_size) {
    result.push_back(rest_bytes.substr(0, e));
    rest_bytes.remove_prefix(e);
  }

  return result;
}

}  // namespace yadcc::client
