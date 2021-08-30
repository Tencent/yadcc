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

#ifndef YADCC_CLIENT_COMMON_OUTPUT_STREAM_H_
#define YADCC_CLIENT_COMMON_OUTPUT_STREAM_H_

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "blake3/blake3.h"
#include "zstd/zstd.h"

#include "yadcc/client/common/logging.h"
#include "yadcc/client/common/span.h"

namespace yadcc::client {

// `OutputStream` accepts binary bytes and, optionally, compress them on the
// fly.
//
// For our purposes, this allows us to overlap preprocessing and compression and
// reduce memory footprint.
class OutputStream {
 public:
  virtual ~OutputStream() = default;

  // Write some bytes into the stream.
  //
  // Not using `std::byte` due to extra coding efforts with that type.
  virtual void Write(const char* data, std::size_t bytes) = 0;
};

// An output stream that merges writes as-is (i.e., no compression is done.)
class TransparentOutputStream : public OutputStream {
 public:
  void Write(const char* data, std::size_t bytes) override;

  const std::string& Get() { return buffer_; }

 private:
  std::string buffer_;
};

// This output stream compresses writes on-the-fly using Zstd compression
// algorithm.
//
// TODO(luobogao): Zstd supports pre-trained dictionary to improve both
// compression ratio and speed SIGNIFICANTLY. Let's see if we can make this
// possible with our implementation. (e.g., by loading dictionary from our local
// daemon.)
class ZstdCompressedOutputStream : public OutputStream {
 public:
  ZstdCompressedOutputStream();

  // Compress bytes.
  void Write(const char* data, std::size_t bytes) override;

  // Flush compressor's internal buffer and merge the result.
  //
  // TODO(luobogao): We can return an array of chunks here. Given that our
  // `DaemonCall` has supported gather I/O, this eliminates a needless copy
  // (beside a large memory allocation).
  std::string FlushAndGet();

 private:
  void FlushCompressorBuffer();

 private:
  inline static constexpr auto kChunkSize = 128 * 1024;

  struct Chunk {
    std::unique_ptr<char[]> buffer{new char[kChunkSize]};
    std::size_t used = 0;
  };

  std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> ctx_{ZSTD_createCCtx(),
                                                            &ZSTD_freeCCtx};
  std::vector<Chunk> chunks_{1};
};

// This stream hashes bytes written to it with BLAKE3 algorithm.
class Blake3OutputStream : public OutputStream {
 public:
  Blake3OutputStream();

  // Hash bytes.
  void Write(const char* data, std::size_t bytes) override;

  // Flush any internal state.
  void Finalize();

  // Get cache key
  std::string GetSourceDigest() const;

 private:
  blake3_hasher state_;
  uint8_t digest_[BLAKE3_OUT_LEN];
};

// Input of this stream is forwarded as-is to all streams assigned to it.`.
class ForwardingOutputStream : public OutputStream {
 public:
  explicit ForwardingOutputStream(Span<OutputStream*> streams)
      : streams_(streams) {}

  void Write(const char* data, std::size_t bytes) override {
    for (auto&& e : streams_) {
      e->Write(data, bytes);
    }
  }

 private:
  Span<OutputStream*> streams_;
};

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMMON_OUTPUT_STREAM_H_
