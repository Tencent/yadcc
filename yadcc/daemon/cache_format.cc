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

#include "yadcc/daemon/cache_format.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "flare/base/buffer/zero_copy_stream.h"
#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding.h"
#include "flare/base/endian.h"
#include "flare/base/string.h"

#include "yadcc/daemon/cache_meta.pb.h"
#include "yadcc/daemon/common_flags.h"

namespace yadcc::daemon {

namespace {

// Wire format:
//
// [CacheEntryHeader][CacheMeta][Object file]

// Little endian.
struct CacheEntryHeader {
  std::uint32_t meta_size;
  std::uint32_t files_size;

  // Not used. For the moment only Zstd is possible.
  std::uint32_t compression_algorithm;
};

static_assert(sizeof(CacheEntryHeader) == 12);

}  // namespace

//////////////////////////////////////
// Compilation cache entry itself.  //
//////////////////////////////////////

std::string GetCxxCacheEntryKey(const EnvironmentDesc& desc,
                                const std::string_view& invocation_arguments,
                                const std::string_view& source_digest) {
  // Hash them all togher to keep key length managable.
  return flare::Format("yadcc-cxx2-entry-{}",
                       flare::EncodeHex(flare::Blake3(
                           {"using-extra-info", desc.compiler_digest(),
                            invocation_arguments, source_digest})));
}

flare::NoncontiguousBuffer WriteCacheEntry(const CacheEntry& result) {
  flare::NoncontiguousBufferBuilder builder;

  CacheMeta meta;
  meta.set_exit_code(result.exit_code);
  meta.set_standard_output(result.standard_output);
  meta.set_standard_error(result.standard_error);
  *meta.mutable_extra_info() = result.extra_info;
  meta.set_files_digest(flare::Blake3(result.files));

  CacheEntryHeader header;
  header.meta_size = meta.ByteSizeLong();
  header.files_size = result.files.ByteSize();
  flare::ToLittleEndian(&header.meta_size);
  flare::ToLittleEndian(&header.files_size);

  builder.Append(&header, sizeof(CacheEntryHeader));
  {
    flare::NoncontiguousBufferOutputStream nbos(&builder);
    FLARE_CHECK(meta.SerializeToZeroCopyStream(&nbos));  // How can it fail?
  }
  builder.Append(result.files);
  return builder.DestructiveGet();
}

std::optional<CacheEntry> TryParseCacheEntry(
    flare::NoncontiguousBuffer buffer) {
  CacheEntryHeader header;

  if (buffer.ByteSize() < sizeof(header)) {
    return std::nullopt;
  }
  flare::FlattenToSlow(buffer, &header, sizeof(header));
  flare::FromLittleEndian(&header.meta_size);
  flare::FromLittleEndian(&header.files_size);

  if (buffer.ByteSize() <
      sizeof(header) + header.meta_size + header.files_size) {
    return std::nullopt;
  }

  buffer.Skip(sizeof(header));
  auto meta = buffer.Cut(header.meta_size);
  auto files = buffer.Cut(header.files_size);

  CacheMeta meta_msg;
  {
    flare::NoncontiguousBufferInputStream nbis(&meta);
    if (!meta_msg.ParseFromZeroCopyStream(&nbis)) {
      return std::nullopt;
    }
  }
  if (flare::Blake3(files) != meta_msg.files_digest()) {
    return std::nullopt;
  }

  return CacheEntry{.exit_code = meta_msg.exit_code(),
                    .standard_output = meta_msg.standard_output(),
                    .standard_error = meta_msg.standard_error(),
                    .extra_info = meta_msg.extra_info(),
                    .files = files};
}

}  // namespace yadcc::daemon
