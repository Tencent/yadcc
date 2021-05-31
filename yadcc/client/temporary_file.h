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

#ifndef YADCC_CLIENT_TEMPORARY_FILE_H_
#define YADCC_CLIENT_TEMPORARY_FILE_H_

#include <string>

namespace yadcc::client {

// Create a temporary file for our use. The temporary file is automatically
// deleted when this object is destroyed.
class TemporaryFile {
 public:
  TemporaryFile();
  ~TemporaryFile();

  const std::string& GetPath() const noexcept { return path_; }

  std::string ReadAll() const;
  void Write(const std::string& data) const;

 private:
  int fd_;
  std::string path_;
};

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_TEMPORARY_FILE_H_
