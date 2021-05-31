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

#include "yadcc/client/compilation_saas.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <utility>
#include <vector>

#include "thirdparty/blake3/blake3.h"
#include "thirdparty/fmt/format.h"
#include "thirdparty/jsoncpp/json.h"
#include "thirdparty/zstd/zstd.h"

#include "yadcc/client/command.h"
#include "yadcc/client/daemon_call.h"
#include "yadcc/client/env_options.h"
#include "yadcc/client/io.h"
#include "yadcc/client/logging.h"
#include "yadcc/client/temporary_file.h"

using namespace std::literals;

namespace yadcc::client {

namespace {

// Removal of the arguments below does the following:
//
// - Dependency should not be generated on the cloud.
const std::unordered_set<std::string_view> kIgnoredArgs = {
    "-MMD", "-MF", "-MD", "-MT", "-MP", "-o", "-Wmissing-include-dirs"};
const std::vector<std::string_view> kIgnoredArgPrefixes = {
    "-Wp,-MMD", "-Wp,-MF", "-Wp,-MD",  "-Wp,-MD",
    "-Wp,-MP",  "-I",      "-include", "-isystem"};

std::pair<std::uint64_t, std::uint64_t> GetMtimeAndSize(
    const std::string& file) {
  struct stat result;
  PCHECK(lstat(file.c_str(), &result) == 0);
  return std::pair(result.st_mtime, result.st_size);
}

std::string Blake3Digest(const std::string& file) {
  auto data = ReadAll(file);
  blake3_hasher state;
  blake3_hasher_init(&state);
  blake3_hasher_update(&state, data.data(), data.size());
  uint8_t output[BLAKE3_OUT_LEN];
  blake3_hasher_finalize(&state, output, BLAKE3_OUT_LEN);
  char hex[BLAKE3_OUT_LEN * 2 + 1] = {0};
  for (int i = 0; i != BLAKE3_OUT_LEN; ++i) {
    snprintf(hex + i * 2, 2 + 1, "%02x", output[i]);
  }
  return std::string(hex, hex + BLAKE3_OUT_LEN * 2);
}

// Decompress bytes compressed by zstd.
std::string DecompressUsingZstd(const std::string& from) {
  std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx{ZSTD_createDCtx(),
                                                           &ZSTD_freeDCtx};
  std::string frame_buffer(ZSTD_DStreamOutSize(), 0);
  std::string decompressed;

  ZSTD_inBuffer in_ref = {.src = from.data(), .size = from.size(), .pos = 0};
  std::size_t decompression_result = 0;
  while (in_ref.pos != in_ref.size) {
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    decompression_result = ZSTD_decompressStream(ctx.get(), &out_ref, &in_ref);
    CHECK(!ZSTD_isError(decompression_result));
    decompressed.append(frame_buffer.data(), out_ref.pos);
  }
  while (decompression_result) {  // Flush Zstd's internal buffer.
    ZSTD_outBuffer out_ref = {
        .dst = frame_buffer.data(), .size = frame_buffer.size(), .pos = 0};
    decompression_result = ZSTD_decompressStream(ctx.get(), &out_ref, &in_ref);
    CHECK(!ZSTD_isError(decompression_result));
    decompressed.append(frame_buffer.data(), out_ref.pos);
  }
  return decompressed;
}

std::string CutChunkFromView(std::string_view* view) {
  auto delim = view->find_first_of('\n');
  char* ptr;
  auto length = strtol(view->data(), &ptr, 16);
  if (ptr - view->data() != delim - 1 /* '\r\n' */) {
    return "";  // FIXME: We'd better make error more explicit.
  }
  *view = view->substr(delim + 1);
  if (length + 2 > view->size()) {
    return "";
  }
  auto str = std::string(view->substr(0, length));
  *view = view->substr(length + 2 /* "\r\n" */);
  return str;
}

std::pair<std::string, std::string> ParseChunks(const std::string& from) {
  std::string_view view = from;
  auto first = CutChunkFromView(&view);
  auto second = CutChunkFromView(&view);
  return {first, second};
}

std::optional<std::string> SubmitCompilationTask(
    const CompilerArgs& args, RewriteResult rewritten_source) {
  // `-o` is not set here. It's up to the compile-server to choose what it sees
  // fit.
  auto header_args =
      "X-Compiler-Invocation-Arguments: " +
      args.Rewrite(kIgnoredArgs, kIgnoredArgPrefixes,
                   {"-fpreprocessed",
                    rewritten_source.directives_only ? "-fdirectives-only" : "",
                    "-x", rewritten_source.language, "-"},
                   false)
          .ToCommandLine(false);
  auto&& compiler = args.GetCompiler();
  auto&& [mtime, size] = GetMtimeAndSize(compiler);
  std::vector<std::string> headers{
      "Content-Encoding: application/zstd",
      fmt::format("X-Requestor-Process-Id: {}", getpid()),
      fmt::format("X-Compiler-Path: {}", compiler),
      fmt::format("X-Compiler-Modification-Time: {}", mtime),
      fmt::format("X-Compiler-Size: {}", size),
      fmt::format("X-Source-Path: {}", rewritten_source.source_path),
      fmt::format("X-Cache-Control: {}",
                  static_cast<int>(rewritten_source.cache_control)),
      // Not applicable is caching is not allowed.
      fmt::format("X-Source-Digest: {}", rewritten_source.source_digest),
      header_args,
      // We always wait for completion in a separate call.
      "X-Milliseconds-To-Wait: 0"};

  auto result = DaemonCall("/local/submit_task", headers,
                           rewritten_source.zstd_rewritten, 5s);
  if (result.status == 400) {
    // Compiler digest is not known to the daemon?
    headers.push_back(
        fmt::format("X-Compiler-Digest: {}", Blake3Digest(compiler)));
    // Retry with compiler-digest provided.
    result = DaemonCall("/local/submit_task", headers,
                        rewritten_source.zstd_rewritten, 5s);
  }
  auto&& [status, body] = result;
  if (status != 200) {
    LOG_ERROR("Local daemon rejected our submission: [{}] {}", status, body);
    return {};
  }

  // Wait until the compilation completes.
  Json::Value jsv;
  if (!Json::Reader().parse(body, jsv)) {
    LOG_ERROR("Unexpected: Invalid response from delegate daemon.");
    return {};
  }
  return jsv["task_id"].asString();

  // `rewritten_source` is freed here, after we submitted the task, and before
  // we start waiting (which can be long) for the compilation result. This helps
  // reduce our memory footprint. Preprocessed source code (even compressed) can
  // be as large as several megabytes (or more).
}

CompilationResult WaitForCompilationTask(const std::string& task_id) {
  do {
    auto req_body = fmt::format(
        "{{\"task_id\": \"{}\", \"milliseconds_to_wait\": 10000}}", task_id);
    auto&& [status, body] = DaemonCall(
        "/local/wait_for_task", {"Content-Type: application/json"}, req_body,
        15s /* Must be greater than `milliseconds_to_wait` */);
    if (status == 503) {
      continue;
    } else if (status == 404) {
      LOG_WARN("Our task is forgotten by delegate daemon.");
      return {-1};
    } else if (status != 200) {
      LOG_ERROR("Unexpected HTTP status code [{}] from delegate daemon: {}",
                status, body);
      return {-1};
    }

    // Parse the chunk then.
    auto&& [msg, object] = ParseChunks(body);
    Json::Value jsv;
    if (!Json::Reader().parse(msg, jsv)) {
      LOG_ERROR("Unexpected: Malformed response from delegate daemon.");
      return {-1};
    }

    CompilationResult result = {
        jsv["exit_code"].asInt(), jsv["output"].asString(),
        jsv["error"].asString(), DecompressUsingZstd(object)};
    LOG_DEBUG(
        "Compilation result: exit_code {}, stdout {} bytes, stderr {} bytes, "
        "object file {} bytes.",
        result.exit_code, result.output.size(), result.error.size(),
        result.bytes.size());
    return result;
  } while (true);
}

// Well by "private cloud" I mean "locally".
CompilationResult CompileOnPrivateCloud(const CompilerArgs& args,
                                        RewriteResult rewritten_source) {
  TemporaryFile tempfile;

  auto cmdline = args.Rewrite(
      kIgnoredArgs, kIgnoredArgPrefixes,
      {"-fpreprocessed",
       rewritten_source.directives_only ? "-fdirectives-only" : "", "-o",
       tempfile.GetPath(), "-x", rewritten_source.language, "-"},
      false);
  auto&& [ec, output, error] = ExecuteCommand(
      cmdline, DecompressUsingZstd(rewritten_source.zstd_rewritten) /* ... */);

  // The caller should retry locally if the compilation fails. We don't bother
  // doing that.
  return {ec, output, error, tempfile.ReadAll()};
}

CompilationResult CompileOnPublicCloud(const CompilerArgs& args,
                                       RewriteResult rewritten_source) {
  LOG_TRACE("Preparing to submit compilation task.");
  auto task_id = SubmitCompilationTask(args, std::move(rewritten_source));
  if (!task_id) {
    LOG_WARN("Failed to submit task to the cloud.");
    // TODO(luobogao): Why not retry a few times before giving up?
    return {-1};
  }

  LOG_TRACE("Compilation task [{}] is successfully submitted.", *task_id);
  return WaitForCompilationTask(*task_id);
}

}  // namespace

CompilationResult CompileOnCloud(const CompilerArgs& args,
                                 RewriteResult rewritten_source) {
  CHECK(!rewritten_source.zstd_rewritten.empty());  // It can't be.
  LOG_TRACE("Preprocessed source code (compressed) is [{}] bytes.",
            rewritten_source.zstd_rewritten.size());
  if (GetOptionCompileOnPrivateCloud()) {
    return CompileOnPrivateCloud(args, std::move(rewritten_source));
  }
  return CompileOnPublicCloud(args, std::move(rewritten_source));
}

}  // namespace yadcc::client
