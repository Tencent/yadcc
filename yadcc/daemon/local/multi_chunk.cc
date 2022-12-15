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

#include "yadcc/daemon/local/multi_chunk.h"

#include <optional>
#include <string>
#include <vector>

#include "flare/base/buffer.h"
#include "flare/base/string.h"

namespace yadcc::daemon::local {

flare::NoncontiguousBuffer MakeMultiChunk(
    std::vector<flare::NoncontiguousBuffer> buffers) {
  if (buffers.empty()) {
    return {};
  }
  std::string result;
  for (auto&& e : buffers) {
    result += std::to_string(e.ByteSize()) + ",";
  }
  result.pop_back();
  result += "\r\n";

  flare::NoncontiguousBufferBuilder builder;
  builder.Append(result);
  for (auto&& e : buffers) {
    builder.Append(std::move(e));
  }
  return builder.DestructiveGet();
}

std::optional<std::vector<flare::NoncontiguousBuffer>> TryParseMultiChunk(
    flare::NoncontiguousBuffer buffer) {
  std::vector<flare::NoncontiguousBuffer> parts;
  if (buffer.Empty()) {
    return parts;
  }

  auto sizes = flare::FlattenSlowUntil(
      buffer, "\r\n",
      // Accepts at most 1024 parts. `+ 1` for separator. Hopefully this is
      // sufficient.
      (std::numeric_limits<std::size_t>::digits10 + 1) * 1024);
  if (!flare::EndsWith(sizes, "\r\n")) {
    return std::nullopt;
  }
  buffer.Skip(sizes.size());
  sizes.pop_back();  // '\r'.
  sizes.pop_back();  // '\n'.

  std::vector<std::size_t> parsed_sizes;
  std::size_t total_size = 0;
  for (auto&& e : flare::Split(sizes, ",")) {
    if (auto opt = flare::TryParse<std::size_t>(e)) {
      parsed_sizes.push_back(*opt);
      total_size += *opt;
    } else {
      return std::nullopt;
    }
  }
  if (buffer.ByteSize() != total_size) {
    return std::nullopt;
  }
  for (auto&& e : parsed_sizes) {
    parts.push_back(buffer.Cut(e));
  }
  return parts;
}

}  // namespace yadcc::daemon::local
