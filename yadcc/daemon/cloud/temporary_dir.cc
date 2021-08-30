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

#include "yadcc/daemon/cloud/temporary_dir.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "flare/base/handle.h"
#include "flare/base/logging.h"
#include "flare/base/random.h"
#include "flare/base/string.h"
#include "flare/base/tsc.h"

#include "yadcc/common/dir.h"
#include "yadcc/common/io.h"

namespace yadcc::daemon::cloud {

TemporaryDir::TemporaryDir(const std::string& prefix) {
  // Hopefully this directory name won't collide.
  dir_ = flare::Format("{}/yadcc_{}_{}", prefix, flare::ReadTsc(),
                       flare::Random());
  Mkdirs(dir_);
  is_alive_ = true;
}

TemporaryDir::~TemporaryDir() { Dispose(); }

TemporaryDir::TemporaryDir(TemporaryDir&& bundle) noexcept {
  dir_ = std::move(bundle.dir_);
  is_alive_ = std::exchange(bundle.is_alive_, false);
}

TemporaryDir& TemporaryDir::operator=(TemporaryDir&& bundle) noexcept {
  if (this == &bundle) {
    return *this;
  }
  if (is_alive_) {
    Dispose();
  }
  dir_ = std::move(bundle.dir_);
  is_alive_ = std::exchange(bundle.is_alive_, false);
  return *this;
}

std::string TemporaryDir::GetPath() const { return dir_; }

std::vector<std::pair<std::string, flare::NoncontiguousBuffer>>
TemporaryDir::ReadAll(const std::string& subdir) {
  FLARE_CHECK(is_alive_);

  std::vector<std::pair<std::string, flare::NoncontiguousBuffer>> files;
  auto root_dir = flare::Format("{}/{}", dir_, subdir);
  auto entries = EnumerateDirRecursively(root_dir);
  for (auto&& e : entries) {
    if (e.is_regular) {
      auto path = flare::Format("{}/{}", root_dir, e.name);
      flare::Handle fd(open(path.c_str(), O_RDONLY));
      FLARE_PCHECK(fd, "Failed to read [{}].", path);
      flare::NoncontiguousBufferBuilder builder;
      FLARE_CHECK(ReadAppend(fd.Get(), &builder) == ReadStatus::Eof);
      FLARE_VLOG(10, "Read [{}] bytes from [{}].", builder.ByteSize(), path);
      files.emplace_back(e.name /* Relative name */, builder.DestructiveGet());
    }
  }
  return files;
}

void TemporaryDir::Dispose() {
  if (is_alive_) {
    RemoveDirs(dir_);
    is_alive_ = false;
  }
}

}  // namespace yadcc::daemon::cloud
