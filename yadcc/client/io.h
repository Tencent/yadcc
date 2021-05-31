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

#ifndef YADCC_CLIENT_IO_H_
#define YADCC_CLIENT_IO_H_

#include <string>

namespace yadcc::client {

// Make `fd` non-blocking.
void SetNonblocking(int fd);

// Read at most `bytes` bytes from `fd` into `buffer`.
//
// Returns bytes actually read, or negative on error. `EINTR` is handled by this
// method. If desired, you can handle `EAGAIN` yourself (when an error is
// returned).
std::ptrdiff_t ReadBytes(int fd, char* buffer, std::size_t bytes);

// Write [data.data() + starts_at, ...) to `fd`.
//
// Returns: bytes written.
std::ptrdiff_t WriteTo(int fd, const std::string& data, std::size_t starts_at);

// I/O from a disk file.
std::string ReadAll(const std::string& path);
void WriteAll(const std::string& filename, const std::string_view& data);

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_IO_H_
