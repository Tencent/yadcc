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

#include "thirdparty/zstd/zstd.h"

#include "yadcc/client/logging.h"

namespace yadcc::client {

void TransparentOutputStream::Write(const char* data, std::size_t bytes) {
  buffer_.append(data, bytes);
}

ZstdCompressedOutputStream::ZstdCompressedOutputStream() {
  // Trade space for speed.
  ZSTD_initCStream(ctx_.get(), ZSTD_fast);
}

void ZstdCompressedOutputStream::Write(const char* data, std::size_t bytes) {
  ZSTD_inBuffer in_ref = {.src = data, .size = bytes, .pos = 0};

  while (in_ref.pos != in_ref.size) {
    if (chunks_.back().used == kChunkSize) {
      chunks_.emplace_back();
    }

    // Compress a chunk of bytes.
    auto bytes_available = kChunkSize - chunks_.back().used;
    ZSTD_outBuffer out_ref = {
        .dst = chunks_.back().buffer.get() + chunks_.back().used,
        .size = bytes_available,
        .pos = 0};
    auto result =
        ZSTD_compressStream2(ctx_.get(), &out_ref, &in_ref, ZSTD_e_continue);
    CHECK(!ZSTD_isError(result), "Failed to compress bytes given.");

    // Update bytes used.
    chunks_.back().used += out_ref.pos;
  }
}

std::string ZstdCompressedOutputStream::FlushAndGet() {
  FlushCompressorBuffer();

  std::size_t bytes = 0;

  for (auto&& e : chunks_) {
    bytes += e.used;
  }

  std::string result;
  result.reserve(bytes);
  for (auto&& e : chunks_) {
    result.append(e.buffer.get(), e.used);
  }

  chunks_.clear();  // Free the chunks ASAP, so as to save memory.
  return result;
}

void ZstdCompressedOutputStream::FlushCompressorBuffer() {
  // Nothing to compress, we're here only to flush its internal buffer.
  ZSTD_inBuffer in_ref = {};

  while (true) {
    if (chunks_.back().used == kChunkSize) {
      chunks_.emplace_back();
    }

    auto bytes_available = kChunkSize - chunks_.back().used;
    ZSTD_outBuffer out_ref = {
        .dst = chunks_.back().buffer.get() + chunks_.back().used,
        .size = bytes_available,
        .pos = 0};
    auto result =
        ZSTD_compressStream2(ctx_.get(), &out_ref, &in_ref, ZSTD_e_end);
    CHECK(!ZSTD_isError(result), "Failed to compress bytes given.");
    chunks_.back().used += out_ref.pos;

    if (result == 0) {  // Fully flushed then.
      break;
    }
  }
}

CachingParametersOutputStream::CachingParametersOutputStream(
    bool ignore_ts_macros)
    : ignore_ts_macros_(ignore_ts_macros) {
  blake3_hasher_init(&state_);
}

void CachingParametersOutputStream::Write(const char* data, std::size_t bytes) {
  if (cacheable_) {
    blake3_hasher_update(&state_, data, bytes);

    if (!ignore_ts_macros_) {
      // Dirty but should work mostly.
      //
      // FIXME: If these macros appear on bound of `data`, this won't work.
      // CAUTION: `memmem` is RATHER slow (these `memmem`s cost more CPU cycles
      // than zstd in our test.)
      if (memmem(data, bytes, "__TIME__", 8) ||
          memmem(data, bytes, "__DATE__", 8) ||
          memmem(data, bytes, "__TIMESTAMP__", 13)) {
        cacheable_ = false;
        LOG_TRACE(
            "Uncacheable preprocessed code: `__TIME__` / `__DATE__` or "
            "`__TIMESTAMP__` is present.");
      }
    }
  }
}

void CachingParametersOutputStream::Finalize() {
  blake3_hasher_finalize(&state_, digest_, BLAKE3_OUT_LEN);
}

std::string CachingParametersOutputStream::GetSourceDigest() const {
  std::string result;
  for (size_t i = 0; i < sizeof(digest_); i++) {
    char buf[10];
    snprintf(buf, sizeof(buf), "%02x", digest_[i]);
    result += buf;
  }
  return result;
}

}  // namespace yadcc::client
