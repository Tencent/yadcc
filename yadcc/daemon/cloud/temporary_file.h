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

#ifndef YADCC_DAEMON_CLOUD_TEMPORARY_FILE_H_
#define YADCC_DAEMON_CLOUD_TEMPORARY_FILE_H_

#include <string>

#include "flare/base/buffer.h"

namespace yadcc::daemon::cloud {

// Create a temporary file for our use. The temporary file is automatically
// deleted when this object is destroyed.
//
// There's a same class in `yadcc/client`. We cannot share the code, however.
// The implementation in `yadcc/client` cannot use Flare's logging facility.
class TemporaryFile {
 public:
  TemporaryFile() = default;
  explicit TemporaryFile(const std::string& prefix);  // `/absolute-path/to/...`
  ~TemporaryFile();

  TemporaryFile(TemporaryFile&& file) noexcept;
  TemporaryFile& operator=(TemporaryFile&& file) noexcept;

  int fd() const noexcept { return fd_; }

  const std::string& GetPath() const noexcept { return path_; }

  flare::NoncontiguousBuffer ReadAll() const;
  void Write(const flare::NoncontiguousBuffer& data) const;

  void Close();

 private:
  int fd_ = 0;
  std::string path_;
};

}  // namespace yadcc::daemon::cloud

#endif  //  YADCC_DAEMON_CLOUD_TEMPORARY_FILE_H_
