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

#include "yadcc/client/output_stream.h"

#undef CHECK  // ...
#undef PCHECK

#include <memory>

#include "thirdparty/googletest/gtest/gtest.h"

// Using Flare in UTs does not matter.
#include "flare/base/logging.h"
#include "flare/base/random.h"

namespace yadcc::client {

// Copied from `compilation_saas.cc`. Hopefully it's correct.
std::string DecompressUsingZstd(const std::string& from) {
  std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx{ZSTD_createDCtx(),
                                                           &ZSTD_freeDCtx};
  std::string frame_buffer(ZSTD_DStreamOutSize(), 0);
  std::string decompressed;

  ZSTD_inBuffer in_ref = {.src = from.data(), .size = from.size(), .pos = 0};
  std::size_t decompression_result = 0;
  while (in_ref.pos != in_ref.size) {
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    decompression_result = ZSTD_decompressStream(ctx.get(), &out_ref, &in_ref);
    FLARE_CHECK(!ZSTD_isError(decompression_result));
    decompressed.append(frame_buffer.data(), out_ref.pos);
  }
  while (decompression_result) {
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    decompression_result = ZSTD_decompressStream(ctx.get(), &out_ref, &in_ref);
    FLARE_CHECK(!ZSTD_isError(decompression_result));
    decompressed.append(frame_buffer.data(), out_ref.pos);
  }
  return decompressed;
}

std::string RandomeBytesOfSize(std::size_t size) {
  int x = 0;
  std::string result;
  result.reserve(size + 8);
  while (result.size() < size) {
    std::uint64_t value = flare::Random<std::uint64_t>();
    if (x++ % 2) {
      result.append(reinterpret_cast<const char*>(&value), 8);
    } else {
      result.append(std::to_string(value));  // Make it more compressible.
    }
  }
  result.resize(size);
  return result;
}

TEST(ZstdCompressedOutputStream, Torture) {
  for (int i = 0; i != 100; ++i) {
    std::string expected;
    ZstdCompressedOutputStream os;
    for (int j = 0; j != 100; ++j) {
      auto bytes = RandomeBytesOfSize(flare::Random(10000));
      os.Write(bytes.data(), bytes.size());
      expected += bytes;
    }
    // Don't use `EXPECT_EQ` here, otherwise expectation failure would mess your
    // screen.
    EXPECT_TRUE(DecompressUsingZstd(os.FlushAndGet()) == expected);
  }
}

}  // namespace yadcc::client
