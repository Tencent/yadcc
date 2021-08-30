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

#ifndef YADCC_CLIENT_COMMON_MULTI_CHUNK_H_
#define YADCC_CLIENT_COMMON_MULTI_CHUNK_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yadcc::client {

// Generates a header (CRLF included) for multiple chunk. This is mostly used
// for performance purposes.
std::string MakeMultiChunkHeader(const std::vector<std::string_view>& parts);

// Concatenate multiple chunks into a big one. The result can be subsequently
// parsed by `TryParseMultiChunk`.
//
// Resulting buffer:
//
// size1,size2,size3,size4\r\n
// (size1 bytes)(size2 bytes)(size3 bytes)(size4 bytes)
std::string MakeMultiChunk(const std::vector<std::string_view>& parts);

// Parse chunks from `view`.
std::optional<std::vector<std::string_view>> TryParseMultiChunk(
    const std::string_view& view);

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMMON_MULTI_CHUNK_H_
