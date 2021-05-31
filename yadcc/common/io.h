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

#ifndef YADCC_COMMON_IO_H_
#define YADCC_COMMON_IO_H_

#include "flare/base/buffer.h"

// Same as `flare/client/io.h` except here we deal with noncontiguous buffer
// instead of `std::string.`
//
// TODO(luobogao): Perhaps we can move these method into `flare/base/buffer.h`.
namespace yadcc {

enum class ReadStatus {
  Eof,            // All of the data has been read.
  TryAgainLater,  // EAGAIN / EWOULDBLOCK
  Failed,         // Other failure.
};

// Make `fd` non-blocking.
void SetNonblocking(int fd);

// Read from `fd` and append the data into `to`.
ReadStatus ReadAppend(int fd, flare::NoncontiguousBufferBuilder* to);

// Write [data.data() + starts_at, ...) to `fd`.
//
// Returns: bytes written.
std::ptrdiff_t WriteTo(int fd, const flare::NoncontiguousBuffer& data);

}  // namespace yadcc

#endif  //  YADCC_COMMON_IO_H_
