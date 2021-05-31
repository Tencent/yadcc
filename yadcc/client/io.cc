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

#include "yadcc/client/io.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <string>

#include "yadcc/client/logging.h"

namespace yadcc::client {

void SetNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  CHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

std::ptrdiff_t ReadBytes(int fd, char* buffer, std::size_t bytes) {
  std::ptrdiff_t bytes_read;
  while (true) {
    bytes_read = read(fd, buffer, bytes);
    if (bytes_read == -1 && errno == EINTR) {
      continue;
    }
    return bytes_read;
  }
}

std::ptrdiff_t WriteTo(int fd, const std::string& data, std::size_t starts_at) {
  CHECK(starts_at <= data.size());
  do {
    int bytes = write(fd, data.data() + starts_at, data.size() - starts_at);
    if (bytes > 0) {
      return bytes;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return -1;
  } while (true);
}

std::string ReadAll(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  auto data = std::string(std::istreambuf_iterator<char>(input), {});
  CHECK(input, "Failed to read from [{}].", path);
  return data;
}

void WriteAll(const std::string& filename, const std::string_view& data) {
  std::ofstream output(filename, std::ios::binary);
  output.write(data.data(), data.size());
  CHECK(output, "Failed to write to [{}]", filename);
}

}  // namespace yadcc::client
