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

#include "flare/base/buffer/zero_copy_stream.h"
#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding.h"
#include "flare/base/endian.h"
#include "flare/base/string.h"

#include "yadcc/daemon/cache_meta.pb.h"

namespace yadcc::daemon {

namespace {

// Wire format:
//
// [CacheEntryHeader][CacheMeta][Object file]

// Little endian.
struct CacheEntryHeader {
  std::uint32_t meta_size;
  std::uint32_t object_file_size;

  // Not used. For the moment only Zstd is possible.
  std::uint32_t compression_algorithm;
};

static_assert(sizeof(CacheEntryHeader) == 12);

}  // namespace

//////////////////////////////////////
// Compilation cache entry itself.  //
//////////////////////////////////////

std::string GetCacheEntryKey(const EnvironmentDesc& desc,
                             const std::string& invocation_arguments,
                             const std::string_view& source_digest) {
  return flare::Format(
      "yadcc-entry-{}",
      flare::EncodeHex(flare::Blake3(
          {desc.compiler_digest(), invocation_arguments, source_digest})));
}

flare::NoncontiguousBuffer WriteCacheEntry(const CacheEntry& result) {
  flare::NoncontiguousBufferBuilder builder;

  CacheMeta meta;
  meta.set_exit_code(result.exit_code);
  meta.set_standard_output(result.standard_output);
  meta.set_standard_error(result.standard_error);
  meta.set_object_file_digest(flare::Blake3(result.object_file));

  CacheEntryHeader header;
  header.meta_size = meta.ByteSizeLong();
  header.object_file_size = result.object_file.ByteSize();
  flare::ToLittleEndian(&header.meta_size);
  flare::ToLittleEndian(&header.object_file_size);

  builder.Append(&header, sizeof(CacheEntryHeader));
  {
    flare::NoncontiguousBufferOutputStream nbos(&builder);
    FLARE_CHECK(meta.SerializeToZeroCopyStream(&nbos));  // How can it fail?
  }
  builder.Append(result.object_file);
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
  flare::FromLittleEndian(&header.object_file_size);

  if (buffer.ByteSize() <
      sizeof(header) + header.meta_size + header.object_file_size) {
    return std::nullopt;
  }

  buffer.Skip(sizeof(header));
  auto meta = buffer.Cut(header.meta_size);
  auto object_file = buffer.Cut(header.object_file_size);

  CacheMeta meta_msg;
  {
    flare::NoncontiguousBufferInputStream nbis(&meta);
    if (!meta_msg.ParseFromZeroCopyStream(&nbis)) {
      return std::nullopt;
    }
  }
  // For backward compatibility, we test `object_file_digest` before using it.
  //
  // TODO(luobogao): Always verify digest once we have most of our daemons
  // upgraded.
  if (!meta_msg.object_file_digest().empty() &&
      flare::Blake3(object_file) != meta_msg.object_file_digest()) {
    return std::nullopt;
  }
  return CacheEntry{.exit_code = meta_msg.exit_code(),
                    .standard_output = meta_msg.standard_output(),
                    .standard_error = meta_msg.standard_error(),
                    .object_file = object_file};
}

}  // namespace yadcc::daemon
