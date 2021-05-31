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

#include "yadcc/cache/dir.h"

#include <dirent.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

#include "flare/base/logging.h"
#include "flare/base/string.h"

using namespace std::literals;

namespace yadcc::cache {

std::vector<DirEntry> EnumerateDir(const std::string& path) {
  std::unique_ptr<DIR, int (*)(DIR*)> dir{opendir(path.c_str()), &closedir};
  FLARE_PCHECK(!!dir, "Failed to open directory [{}].", path);

  std::vector<DirEntry> result;
  while (auto ent = readdir(dir.get())) {
    auto type = ent->d_type;
    if (ent->d_name == "."sv || ent->d_name == ".."sv) {
      continue;
    }
    DirEntry entry = {.name = ent->d_name,
                      .inode = ent->d_ino,
                      .is_block_dev = !!(type & DT_BLK),
                      .is_char_dev = !!(type & DT_CHR),
                      .is_dir = !!(type & DT_DIR),
                      .is_symlink = !!(type & DT_LNK),
                      .is_regular = !!(type & DT_REG),
                      .is_unix_socket = !!(type & DT_SOCK)};
    result.push_back(entry);
  }
  return result;
}

// Shamelessly copied from https://stackoverflow.com/a/9210960.
void Mkdirs(const std::string& path, mode_t mode) {
  auto copy = path;
  auto dir_path = copy.data();
  for (char* p = strchr(dir_path + 1, '/'); p; p = strchr(p + 1, '/')) {
    *p = '\0';
    if (mkdir(dir_path, mode) == -1) {
      FLARE_PCHECK(errno == EEXIST, "Failed to create directory [{}].",
                   dir_path);
    }
    *p = '/';
  }
  if (mkdir(dir_path, mode) == -1) {
    FLARE_PCHECK(errno == EEXIST, "Failed to create directory [{}].", dir_path);
  }
}

// Shamelessly copied from https://stackoverflow.com/a/42978529
void RemoveDirs(const std::string& path) {
  auto cb = [](const char* pathname, auto, auto, auto) {
    FLARE_PCHECK(remove(pathname) == 0, "Failed to remove [{}].", pathname);
    return 0;
  };
  FLARE_PCHECK(
      nftw(path.c_str(), cb, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS) == 0,
      "Failed to remove [{}].", path);
}

}  // namespace yadcc::cache
