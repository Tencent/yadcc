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

#include "yadcc/client/common/compress.h"

#include <memory>
#include <string>
#include <string_view>

#include "zstd/zstd.h"

#include "yadcc/client/common/logging.h"

namespace yadcc::client {

std::string CompressUsingZstd(const std::string_view& from) {
  std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> ctx{ZSTD_createCCtx(),
                                                           &ZSTD_freeCCtx};
  std::string frame_buffer(ZSTD_DStreamInSize(), 0);
  std::string compressed;

  ZSTD_inBuffer in_ref = {.src = from.data(), .size = from.size(), .pos = 0};

  while (true) {
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    auto result =
        ZSTD_compressStream2(ctx.get(), &out_ref, &in_ref, ZSTD_e_end);
    CHECK(!ZSTD_isError(result), "Failed to compress bytes given.");
    compressed.append(frame_buffer.data(), out_ref.pos);

    if (result == 0) {
      break;
    }
  }
  return compressed;
}

// TODO(luobogao): Error handling.
std::string DecompressUsingZstd(const std::string_view& from) {
  std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx{ZSTD_createDCtx(),
                                                           &ZSTD_freeDCtx};
  std::string frame_buffer(ZSTD_DStreamOutSize(), 0);
  // FIXME: Here we incur several memory (re)allocation on `decompressed`. Can
  // it be optimized?
  std::string decompressed;

  ZSTD_inBuffer in_ref = {.src = from.data(), .size = from.size(), .pos = 0};
  std::size_t decompression_result = 0;
  while (in_ref.pos != in_ref.size) {
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    decompression_result = ZSTD_decompressStream(ctx.get(), &out_ref, &in_ref);
    CHECK(!ZSTD_isError(decompression_result));
    decompressed.append(frame_buffer.data(), out_ref.pos);
  }
  while (decompression_result) {  // Flush Zstd's internal buffer.
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    decompression_result = ZSTD_decompressStream(ctx.get(), &out_ref, &in_ref);
    CHECK(!ZSTD_isError(decompression_result));
    decompressed.append(frame_buffer.data(), out_ref.pos);
  }
  return decompressed;
}

}  // namespace yadcc::client
