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

#ifndef YADCC_DAEMON_LOCAL_MULTI_CHUNK_H_
#define YADCC_DAEMON_LOCAL_MULTI_CHUNK_H_

#include <optional>
#include <vector>

#include "flare/base/buffer.h"

namespace yadcc::daemon::local {

// Almost the same as `yadcc/client/common/multi_chunk.h`, except these ones
// deal with `flare::NoncontiguousBuffer`.

// Pack multiple buffers together.
flare::NoncontiguousBuffer MakeMultiChunk(
    std::vector<flare::NoncontiguousBuffer> buffers);

// Parse chunks from `buffer`.
std::optional<std::vector<flare::NoncontiguousBuffer>> TryParseMultiChunk(
    flare::NoncontiguousBuffer buffer);

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_MULTI_CHUNK_H_
