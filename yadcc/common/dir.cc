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

#include "yadcc/common/dir.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "flare/base/logging.h"
#include "flare/base/string.h"

using namespace std::literals;

namespace yadcc {

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

std::vector<DirEntry> EnumerateDirRecursively(const std::string& path) {
  std::queue<std::string> queue;
  std::vector<DirEntry> result;

  queue.push("");
  while (!queue.empty()) {
    auto temp = EnumerateDir(path + "/" + queue.front());
    for (auto&& e : temp) {
      if (!queue.front().empty()) {
        e.name = queue.front() + "/" + e.name;
      }
    }
    queue.pop();
    result.insert(result.end(), temp.begin(), temp.end());
    for (auto&& e : temp) {
      if (e.is_dir) {
        queue.push(e.name);
      }
    }
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

void RemoveDirs(const std::string& path) {
  std::unique_ptr<DIR, void (*)(DIR*)> dir{opendir(path.c_str()),
                                           [](auto ptr) { closedir(ptr); }};
  FLARE_CHECK(dir, "Failed to open `{}`.", path);
  while (auto ep = readdir(dir.get())) {
    if (ep->d_name == std::string(".") || ep->d_name == std::string("..")) {
      continue;
    }
    auto fullname = flare::Format("{}/{}", path, ep->d_name);
    if (unlink(fullname.c_str()) != 0) {
      FLARE_PCHECK(errno == EISDIR, "Failed to remove [{}].", fullname);
      RemoveDirs(fullname);
    } else {
      FLARE_VLOG(10, "Removed [{}]", fullname);
    }
  }
  FLARE_PCHECK(rmdir(path.c_str()) == 0);
}

std::string GetDirectoryName(const std::string& path) {
  auto pos = path.find_last_of('/');
  FLARE_PCHECK(pos != std::string::npos);
  return path.substr(0, pos);
}

std::string GetCanonicalPath(const std::string& path) {
  char buf[PATH_MAX + 1];
  if (realpath(path.c_str(), buf)) {
    return buf;
  }
  return {};
}

}  // namespace yadcc
