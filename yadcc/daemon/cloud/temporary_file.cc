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

#include "yadcc/daemon/cloud/temporary_file.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <algorithm>
#include <utility>

#include "flare/base/logging.h"

#include "yadcc/common/io.h"

namespace yadcc::daemon::cloud {

TemporaryFile::TemporaryFile(const std::string& prefix) {
  char name_tmpl[256];
  // Appending timestamp to the end of the filename makes me feel much safer.
  snprintf(name_tmpl, sizeof(name_tmpl), "%s/yadcc_%" PRIu64 "_XXXXXX",
           prefix.c_str(),
           static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now()
                                          .time_since_epoch()
                                          .count()));
  fd_ = mkostemps(name_tmpl, 0, O_CLOEXEC);
  FLARE_CHECK(fd_);

  char path[256];
  auto bytes = readlink(("/proc/self/fd/" + std::to_string(fd_)).c_str(), path,
                        sizeof(path));
  FLARE_PCHECK(bytes > 0, "Cannot get temporary file name.");
  path_.assign(path, bytes);
}

TemporaryFile::~TemporaryFile() { Close(); }

TemporaryFile::TemporaryFile(TemporaryFile&& file) noexcept
    : fd_(file.fd_), path_(std::move(file.path_)) {
  file.fd_ = 0;
  file.path_.clear();
}

TemporaryFile& TemporaryFile::operator=(TemporaryFile&& file) noexcept {
  if (this == &file) {
    return *this;
  }
  Close();
  std::swap(fd_, file.fd_);
  std::swap(path_, file.path_);
  return *this;
}

flare::NoncontiguousBuffer TemporaryFile::ReadAll() const {
  FLARE_CHECK(fd(), "No temporary file is opened.");

  flare::NoncontiguousBufferBuilder builder;
  FLARE_PCHECK(lseek(fd_, 0, SEEK_SET) == 0);  // Rewind read pos first.
  CHECK(ReadAppend(fd_, &builder) == ReadStatus::Eof);
  FLARE_VLOG(10, "Read [{}] bytes from [{}].", builder.ByteSize(), path_);
  return builder.DestructiveGet();
}

void TemporaryFile::Write(const flare::NoncontiguousBuffer& data) const {
  FLARE_CHECK(fd(), "No temporary file is opened.");

  FLARE_PCHECK(WriteTo(fd_, data) == data.ByteSize());
}

void TemporaryFile::Close() {
  if (fd_) {
    // `ENOENT` is explicitly ignored. Sometimes our user (e.g., GCC on failure)
    // removes the file before we have a chance to do so.
    FLARE_PCHECK(unlink(path_.c_str()) == 0 || errno == ENOENT,
                 "Failed to remove temporary file [{}]", path_);
    FLARE_PCHECK(close(fd_) == 0, "Failed to close fd [{}]", fd_);

    fd_ = 0;
    path_.clear();
  }
}

}  // namespace yadcc::daemon::cloud
