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

#include "yadcc/client/common/daemon_call.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yadcc/client/common/env_options.h"
#include "yadcc/client/common/io.h"
#include "yadcc/client/common/logging.h"
#include "yadcc/client/common/utility.h"

using namespace std::literals;

// Copied from `flare/rpc/binlog/util/curl.cc`

namespace yadcc::client {

namespace {

enum InternalHttpError {
  ERROR_FAILED_TO_CONNECT = -1,
  ERROR_FAILED_TO_WRITE = -2,
  ERROR_FAILED_TO_READ = -3,
  ERROR_MALFORMED_DATA = -4,
};

// If the header is larger than 8K, we treat it as malformed. (Most of
// browsers do so.).
constexpr auto kMaxHeaderSize = 8192;

DaemonCallGatheredHandler daemon_call_handler;

std::pair<const char*, std::size_t> WritePostHeader(
    const std::string& path, const std::vector<std::string>& headers,
    std::size_t body_size, std::array<char, kMaxHeaderSize>* stack_buffer,
    std::unique_ptr<char[]>* dyn_buffer) {
  constexpr auto kStartLinePrefix = "POST "sv;
  constexpr auto kStartLinePostfix = " HTTP/1.1"sv;
  constexpr auto kLineDelimiter = "\r\n"sv;

  auto content_length_hdr = fmt::format("Content-Length: {}", body_size);
  std::size_t desired_header_size = kStartLinePrefix.size() + path.size() +
                                    kStartLinePostfix.size() +
                                    kLineDelimiter.size();
  for (auto&& h : headers) {
    desired_header_size += h.size() + kLineDelimiter.size();
  }
  desired_header_size += content_length_hdr.size() + kLineDelimiter.size();
  desired_header_size += kLineDelimiter.size();

  char* ptr;
  // I don't think we will ever need a header larger than 128K. Even if we can
  // handle that case, the server likely won't.
  if (desired_header_size < stack_buffer->size()) {
    // No dynamic allocation in this case.
    ptr = stack_buffer->data();
  } else {
    dyn_buffer->reset(new char[desired_header_size]);
    ptr = dyn_buffer->get();
  }

  std::size_t copied = 0;
  auto append_to_buffer = [&](auto&& data) {
    memcpy(ptr + copied, data.data(), data.size());
    copied += data.size();
  };

  append_to_buffer(kStartLinePrefix);
  append_to_buffer(path);
  append_to_buffer(kStartLinePostfix);
  append_to_buffer(kLineDelimiter);
  for (auto&& h : headers) {
    append_to_buffer(h);
    append_to_buffer(kLineDelimiter);
  }
  append_to_buffer(content_length_hdr);
  append_to_buffer(kLineDelimiter);
  append_to_buffer(kLineDelimiter);
  CHECK(copied == desired_header_size);
  return {ptr, copied};
}

int OpenConnectionTo(std::uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    return -1;
  }
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = 0x0100007F;
  addr.sin_port = htons(port);
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOG_DEBUG("Failed to connect to local daemon: %s", strerror(errno));
    PCHECK(close(fd) == 0);
    return -1;
  }
  return fd;
}

bool WaitForEvent(int fd, int event,
                  std::chrono::steady_clock::time_point timeout) {
  pollfd fds;
  fds.fd = fd;
  fds.events = event;
  auto result =
      poll(&fds, 1, std::max(0ns, (timeout - ReadCoarseSteadyClock())) / 1ms);
  PCHECK(result >= 0);
  return result == 1;
}

template <class T, class F>
bool PerformIo(F op, int event, int fd, T&& buffer, std::size_t size,
               std::chrono::steady_clock::time_point timeout) {
  auto done = 0;
  while (done != size && ReadCoarseSteadyClock() < timeout) {
    if (!WaitForEvent(fd, event, timeout)) {
      return false;
    }
    auto bytes = op(fd, buffer + done, size - done);
    if (bytes < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return false;
    }
    done += bytes;
  }
  return done == size;
}

bool TimedWriteV(int fd, const iovec* iov, int iovcnt,
                 std::chrono::steady_clock::time_point timeout) {
  std::size_t size = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size += iov[i].iov_len;
  }

  std::size_t bytes_written = 0;
  while (bytes_written != size && ReadCoarseSteadyClock() < timeout) {
    if (!WaitForEvent(fd, POLLOUT, timeout)) {
      return false;
    }
    CHECK(iovcnt <= 128, "Not implemented.");

    iovec writing[128];
    int writing_iovs = 1;
    std::size_t skipped = 0;
    for (int i = 0; i != iovcnt; ++i) {
      if (skipped + iov[i].iov_len <= bytes_written) {
        skipped += iov[i].iov_len;
        continue;
      }

      auto writing_bytes = skipped + iov[i].iov_len - bytes_written;
      writing[0].iov_base = reinterpret_cast<char*>(iov[i].iov_base) +
                            iov[i].iov_len - writing_bytes;
      writing[0].iov_len = writing_bytes;
      for (int j = i + 1; j != iovcnt; ++j) {
        writing[writing_iovs++] = iov[j];
      }
      break;
    }
    auto bytes = writev(fd, writing, writing_iovs);
    if (bytes < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return false;
    }
    bytes_written += bytes;
  }
  CHECK(bytes_written <= size);
  return bytes_written == size;
}

bool TimedRead(int fd, char* buffer, std::size_t size,
               std::chrono::steady_clock::time_point timeout) {
  return PerformIo(read, POLLIN, fd, buffer, size, timeout);
}

int ReadHttpStatus(int fd, std::chrono::steady_clock::time_point timeout) {
  constexpr auto kStartLinePrefix = "HTTP/1.1 "sv;
  char status_buffer[kStartLinePrefix.size() + 4];  // HTTP/1.1 200 OK
  if (!TimedRead(fd, status_buffer, sizeof(status_buffer), timeout)) {
    return ERROR_FAILED_TO_READ;
  }
  if (memcmp(status_buffer, kStartLinePrefix.data(), kStartLinePrefix.size())) {
    return ERROR_MALFORMED_DATA;
  }
  status_buffer[sizeof(status_buffer) - 1] = 0;  // For `sscanf` to work.
  int status;
  if (sscanf(status_buffer + kStartLinePrefix.size(), "%d", &status) != 1) {
    return ERROR_MALFORMED_DATA;
  }
  return status;
}

std::optional<std::string> ReadHttpBody(
    int fd, std::chrono::steady_clock::time_point timeout) {
  char buffer[kMaxHeaderSize + 1];
  std::size_t bytes_read = 0;

  // Read the entire header first.
  while (true) {
    if (!WaitForEvent(fd, POLLIN, timeout)) {
      return std::nullopt;
    }
    auto bytes = read(fd, buffer + bytes_read, kMaxHeaderSize - bytes_read);
    if (bytes < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return std::nullopt;
    }
    bytes_read += bytes;
    if (memmem(buffer, bytes_read, "\r\n\r\n", 4)) {  // End of header received.
      break;
    }
    if (bytes_read == kMaxHeaderSize) {
      return std::nullopt;  // Header too large.
    }
  }

  // Copy the (partial) body we've read.
  std::string body;
  buffer[kMaxHeaderSize] = 0;  // For `sscanf` below to work.
  if (auto ptr = reinterpret_cast<const char*>(
          memmem(buffer, bytes_read, "\r\n\r\n", 4))) {
    body.assign(ptr + 4, buffer + bytes_read - ptr - 4);
  } else {
    CHECK(0);  // Otherwise we should have bailed out in the loop above.
  }

  // Determine body size.
  constexpr auto kContentLength = "Content-Length:"sv;
  auto length_ptr = reinterpret_cast<const char*>(
      memmem(buffer, bytes_read, kContentLength.data(), kContentLength.size()));
  if (!length_ptr) {
    return std::nullopt;
  }
  length_ptr += kContentLength.size();

  int body_size;
  if (sscanf(length_ptr, "%d", &body_size) != 1) {
    return std::nullopt;
  }
  if (body_size < body.size()) {
    return std::nullopt;
  }
  auto already_read = body.size();
  body.resize(body_size);  // TODO(luobogao): Unnecessarily zero-initialized.
  if (TimedRead(fd, body.data() + already_read, body.size() - already_read,
                timeout)) {
    return body;
  }
  return std::nullopt;
}

DaemonResponse ReadDaemonResponse(
    int fd, std::chrono::steady_clock::time_point timeout) {
  auto status = ReadHttpStatus(fd, timeout);
  if (status < 100) {  // I/O error.
    return DaemonResponse{.status = status};
  }

  if (auto opt = ReadHttpBody(fd, timeout)) {
    return DaemonResponse{.status = status, .body = std::move(*opt)};
  }
  return DaemonResponse{.status = ERROR_FAILED_TO_READ};
}

}  // namespace

DaemonResponse DaemonCall(const std::string& api,
                          const std::vector<std::string>& headers,
                          const std::string& body,
                          std::chrono::nanoseconds timeout) {
  return DaemonCallGathered(api, headers, {body}, timeout);
}

// This is a quick-and-dirty HTTP client. It's by no means "conformant". Our
// only goal here is speed, even in trade of correctness in aspects we don't
// touch.
DaemonResponse DaemonCallGathered(const std::string& api,
                                  const std::vector<std::string>& headers,
                                  const std::vector<std::string_view>& bodies,
                                  std::chrono::nanoseconds timeout) {
  if (daemon_call_handler) {
    return daemon_call_handler(api, headers, bodies, timeout);
  }

  // Build HTTP request header.
  std::array<char, kMaxHeaderSize> stack_buffer;
  std::unique_ptr<char[]> dyn_buffer;
  std::size_t body_size = 0;
  for (auto&& e : bodies) {
    body_size += e.size();
  }
  auto&& [header, size] =
      WritePostHeader(api, headers, body_size, &stack_buffer, &dyn_buffer);

  // Open a connection to daemon.
  auto fd = OpenConnectionTo(GetOptionDaemonPort());
  if (fd == -1) {
    return DaemonResponse{.status = ERROR_FAILED_TO_CONNECT};
  }
  SetNonblocking(fd);

  LOG_DEBUG("Writing {} bytes request.", body_size);
  auto abs_timeout = std::chrono::steady_clock::now() + timeout;

  // Now write the request.
  //
  // We use gather I/O to reduce number of syscalls.
  iovec writing_bytes[128];
  std::size_t writing_parts = 0;
  writing_bytes[writing_parts++] =
      iovec{.iov_base = const_cast<char*>(header), .iov_len = size};
  if (bodies.empty()) {
    if (!TimedWriteV(fd, writing_bytes, writing_parts, abs_timeout)) {
      PCHECK(close(fd) == 0);
      return DaemonResponse{.status = ERROR_FAILED_TO_WRITE};
    }
  } else {
    for (auto iter = bodies.begin(); iter != bodies.end();) {
      while (iter != bodies.end() && writing_parts != 128) {
        writing_bytes[writing_parts++] =
            iovec{.iov_base = const_cast<char*>(iter->data()),
                  .iov_len = iter->size()};
        ++iter;
      }
      if (!TimedWriteV(fd, writing_bytes, writing_parts, abs_timeout)) {
        PCHECK(close(fd) == 0);
        return DaemonResponse{.status = ERROR_FAILED_TO_WRITE};
      }
      // Prepare for the next round (if we cannot send all segments out in a
      // single call.)
      writing_parts = 0;
    }
  }

  // Read response.
  auto result = ReadDaemonResponse(fd, abs_timeout);
  LOG_DEBUG("Received {} bytes response.", result.body.size());
  PCHECK(close(fd) == 0);
  return result;
}

void SetDaemonCallGatheredHandler(DaemonCallGatheredHandler handler) {
  daemon_call_handler = std::move(handler);
}

}  // namespace yadcc::client
