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

#include "yadcc/client/common/io.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "fmt/format.h"

#include "yadcc/client/common/logging.h"

namespace yadcc::client {

void SetNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  CHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

std::ptrdiff_t ReadBytes(int fd, char* buffer, std::size_t bytes) {
  std::ptrdiff_t bytes_read;
  while (true) {
    bytes_read = read(fd, buffer, bytes);
    if (bytes_read == -1 && errno == EINTR) {
      continue;
    }
    return bytes_read;
  }
}

std::ptrdiff_t WriteTo(int fd, const std::string& data, std::size_t starts_at) {
  CHECK(starts_at <= data.size());
  do {
    int bytes = write(fd, data.data() + starts_at, data.size() - starts_at);
    if (bytes > 0) {
      return bytes;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return -1;
  } while (true);
}

std::string ReadAll(const std::string& path) {
  std::unique_ptr<FILE, decltype(&fclose)> file{fopen(path.c_str(), "rb"),
                                                &fclose};
  PCHECK(fseek(file.get(), 0, SEEK_END) == 0);
  auto size = ftell(file.get());
  PCHECK(fseek(file.get(), 0, SEEK_SET) == 0);
  std::string result;
  result.resize(size);  // FIXME: Unnecessarily initialized memory.
  PCHECK(fread(result.data(), 1, size, file.get()) == size);
  return result;
}

void WriteAll(const std::string& filename, const std::string_view& data) {
  std::ofstream output(filename, std::ios::binary);
  output.write(data.data(), data.size());
  CHECK(output, "Failed to write to [{}]", filename);
}

// @sa: `yadcc/common/dir.cc`
void Mkdirs(const std::string& path, mode_t mode) {
  auto copy = path;
  auto dir_path = copy.data();
  for (char* p = strchr(dir_path + 1, '/'); p; p = strchr(p + 1, '/')) {
    *p = '\0';
    if (mkdir(dir_path, mode) == -1) {
      PCHECK(errno == EEXIST, "Failed to create directory [{}].", dir_path);
    }
    *p = '/';
  }
  if (mkdir(dir_path, mode) == -1) {
    PCHECK(errno == EEXIST, "Failed to create directory [{}].", dir_path);
  }
}

void RemoveDirs(const std::string& path) {
  std::unique_ptr<DIR, void (*)(DIR*)> dir{opendir(path.c_str()),
                                           [](auto ptr) { closedir(ptr); }};
  CHECK(dir, "Failed to open `{}`.", path);
  while (auto ep = readdir(dir.get())) {
    if (ep->d_name == std::string(".") || ep->d_name == std::string("..")) {
      continue;
    }
    auto fullname = fmt::format("{}/{}", path, ep->d_name);
    if (unlink(fullname.c_str()) != 0) {
      PCHECK(errno == EISDIR, "Failed to remove [{}].", fullname);
      RemoveDirs(fullname);
    } else {
      LOG_TRACE("Removed [{}]", fullname);
    }
  }
  PCHECK(rmdir(path.c_str()) == 0);
}

}  // namespace yadcc::client
