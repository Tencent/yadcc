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

#ifndef YADCC_COMMON_DIR_H_
#define YADCC_COMMON_DIR_H_

#include <string>
#include <vector>

namespace yadcc {

struct DirEntry {
  std::string name;
  std::uint64_t inode;
  bool is_block_dev : 1;
  bool is_char_dev : 1;
  bool is_dir : 1;
  bool is_symlink : 1;
  bool is_regular : 1;
  bool is_unix_socket : 1;
};

// Enumerate entries in a directory.
//
// `.` / `..` is ignored by this method.
std::vector<DirEntry> EnumerateDir(const std::string& path);

// Enumerate entries in a directory, recursively.
std::vector<DirEntry> EnumerateDirRecursively(const std::string& path);

// Make directories recursively.
void Mkdirs(const std::string& path, mode_t mode = 0755);

// Remove `path` and (if it's non-empty) everything inside it.
void RemoveDirs(const std::string& path);

// Get dir from a file path
std::string GetDirectoryName(const std::string& path);

// Get canonical path name.
std::string GetCanonicalPath(const std::string& path);

}  // namespace yadcc

#endif  // YADCC_COMMON_DIR_H_
