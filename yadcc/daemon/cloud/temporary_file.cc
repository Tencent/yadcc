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

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <utility>

#include "flare/base/chrono.h"
#include "flare/base/logging.h"
#include "flare/base/string.h"

#include "yadcc/common/io.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

TemporaryFile::TemporaryFile(const std::string& prefix) {
  // Appending timestamp to the end of the filename makes me feel much safer.
  auto tmpl_str = flare::Format(
      "{}/yadcc_{}_XXXXXX", prefix,
      std::chrono::high_resolution_clock::now().time_since_epoch() / 1ns);
  char name_tmpl[256];
  snprintf(name_tmpl, sizeof(name_tmpl), "%s", tmpl_str.c_str());
  fd_.Reset(mkostemps(name_tmpl, 0, O_CLOEXEC));
  FLARE_CHECK(fd_);

  char path[256];
  auto bytes = readlink(("/proc/self/fd/" + std::to_string(fd_.Get())).c_str(),
                        path, sizeof(path));
  FLARE_PCHECK(bytes > 0, "Cannot get temporary file name.");
  path_.assign(path, bytes);
}

TemporaryFile::~TemporaryFile() { Close(); }

TemporaryFile::TemporaryFile(TemporaryFile&& file) noexcept
    : fd_(std::move(file.fd_)), path_(std::move(file.path_)) {
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
  FLARE_PCHECK(lseek(fd_.Get(), 0, SEEK_SET) == 0);  // Rewind read pos first.
  CHECK(ReadAppend(fd_.Get(), &builder) == ReadStatus::Eof);
  FLARE_VLOG(10, "Read [{}] bytes from [{}].", builder.ByteSize(), path_);
  return builder.DestructiveGet();
}

void TemporaryFile::Write(const flare::NoncontiguousBuffer& data) const {
  FLARE_CHECK(fd(), "No temporary file is opened.");
  FLARE_PCHECK(WriteTo(fd_.Get(), data) == data.ByteSize());
}

void TemporaryFile::Close() {
  if (fd_) {
    // `ENOENT` is explicitly ignored. Sometimes our user (e.g., GCC on failure)
    // removes the file before we have a chance to do so.
    FLARE_PCHECK(unlink(path_.c_str()) == 0 || errno == ENOENT,
                 "Failed to remove temporary file [{}]", path_);
    fd_.Reset();
    path_.clear();
  }
}

}  // namespace yadcc::daemon::cloud
