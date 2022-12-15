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

#include "yadcc/common/io.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "flare/base/handle.h"
#include "flare/base/logging.h"
#include "flare/base/string.h"

namespace yadcc {

void SetNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  FLARE_PCHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

ReadStatus ReadAppend(int fd, flare::NoncontiguousBufferBuilder* to) {
  // `readv` should optimize the situation, I think.
  while (auto bytes = read(fd, to->data(), to->SizeAvailable())) {
    if (bytes == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return ReadStatus::TryAgainLater;
      }
      return ReadStatus::Failed;
    }
    FLARE_CHECK_GT(bytes, 0);
    to->MarkWritten(bytes);
  }
  return ReadStatus::Eof;
}

std::ptrdiff_t WriteTo(int fd, const flare::NoncontiguousBuffer& bytes) {
  // FIXME: We can traverse `bytes` instead of making a copy and consume it.
  auto copy = bytes;
  std::size_t bytes_written = 0;
  // Well the same code as we've done in sending data through socket via
  // `writev`.
  while (!copy.Empty()) {
    int bytes =
        write(fd, copy.FirstContiguous().data(), copy.FirstContiguous().size());
    if (bytes < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (bytes == 0) {
      break;
    }
    copy.Skip(bytes);
    bytes_written += bytes;
  }
  return bytes_written;
}

flare::NoncontiguousBuffer ReadAll(const std::string& path) {
  flare::Handle fd(open(path.c_str(), O_RDONLY));
  FLARE_PCHECK(fd, "Can't open file [{}]", path);
  flare::NoncontiguousBufferBuilder builder;
  FLARE_PCHECK(ReadAppend(fd.Get(), &builder) == ReadStatus::Eof);
  return builder.DestructiveGet();
}

void WriteAll(const std::string& path, const flare::NoncontiguousBuffer& data) {
  flare::Handle fd(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
  FLARE_PCHECK(fd, "Can't open file [{}]", path);
  FLARE_PCHECK(WriteTo(fd.Get(), data) == data.ByteSize());
}

}  // namespace yadcc
