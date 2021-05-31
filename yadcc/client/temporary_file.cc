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

#include "yadcc/client/temporary_file.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cinttypes>
#include <cstdlib>

#include "yadcc/client/io.h"
#include "yadcc/client/logging.h"

namespace yadcc::client {

TemporaryFile::TemporaryFile() {
  char name_tmpl[256];
  // Appending timestamp to the end of the filename makes me feel much safer.
  snprintf(name_tmpl, sizeof(name_tmpl), "/tmp/yadcc_%" PRIu64 "_XXXXXX",
           static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now()
                                          .time_since_epoch()
                                          .count()));
  fd_ = mkstemp(name_tmpl);
  CHECK(fd_);

  char path[256];
  auto bytes = readlink(("/proc/self/fd/" + std::to_string(fd_)).c_str(), path,
                        sizeof(path));
  PCHECK(bytes > 0, "Cannot get temporary file name.");
  path_.assign(path, bytes);
}

TemporaryFile::~TemporaryFile() {
  // `ENOENT` is explicitly ignored.
  PCHECK(unlink(path_.c_str()) == 0 || errno == ENOENT);
  PCHECK(close(fd_) == 0);
}

std::string TemporaryFile::ReadAll() const {
  struct stat info;
  PCHECK(fstat(fd_, &info) == 0);

  std::string buffer;
  // FIXME: This call leads to unnecessary memset (to zero `std::string`'s
  // internal buffer).
  //
  // Don't worry, though. AFAICT, this entire class is only used for testing
  // purpose.
  buffer.resize(info.st_size);
  PCHECK(lseek(fd_, 0, SEEK_SET) == 0);
  CHECK(ReadBytes(fd_, buffer.data(), buffer.size()) == buffer.size());
  LOG_DEBUG("Read [{}] bytes from [{}].", buffer.size(), path_);
  return buffer;
}

void TemporaryFile::Write(const std::string& data) const {
  CHECK(WriteTo(fd_, data, 0) == data.size());
}

}  // namespace yadcc::client
