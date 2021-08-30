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

#include "yadcc/client/cxx/compilation_saas.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/format.h"
#include "jsoncpp/json.h"

#include "yadcc/client/common/command.h"
#include "yadcc/client/common/compress.h"
#include "yadcc/client/common/daemon_call.h"
#include "yadcc/client/common/env_options.h"
#include "yadcc/client/common/io.h"
#include "yadcc/client/common/logging.h"
#include "yadcc/client/common/multi_chunk.h"
#include "yadcc/client/common/temporary_file.h"
#include "yadcc/client/common/utility.h"

using namespace std::literals;

namespace yadcc::client {

namespace {

struct PatchLocation {
  std::size_t position;
  std::size_t total_size;
  std::size_t suffix_to_keep;
};

using PatchLocations = std::vector<PatchLocation>;

// Removal of the arguments below does the following:
//
// - Dependency should not be generated on the cloud.
const std::unordered_set<std::string_view> kIgnoredArgs = {
    "-MMD", "-MF", "-MD", "-MT", "-MP", "-o", "-Wmissing-include-dirs"};
const std::vector<std::string_view> kIgnoredArgPrefixes = {
    "-Wp,-MMD", "-Wp,-MF", "-Wp,-MD",  "-Wp,-MD",
    "-Wp,-MP",  "-I",      "-include", "-isystem"};

std::string GetExpectedPath(const CompilerArgs& args) {
  std::string desired_path(args.GetOutputFile());
  auto pos = desired_path.find_last_of('.');
  if (pos != std::string::npos) {
    desired_path = desired_path.substr(0, pos);
  }

  // Absolute path is required.
  if (desired_path[0] != '/') {
    char cwd[PATH_MAX];
    PCHECK(getcwd(cwd, sizeof(cwd)));
    std::string tmp(cwd);
    tmp.append("/").append(desired_path);
    desired_path.swap(tmp);
  }
  return desired_path;
}

bool PatchPathOccurrences(
    std::vector<std::pair<std::string, std::string>>* output_files,
    const std::unordered_map<std::string, PatchLocations>& patches,
    const std::string& desired_path) {
  std::set<std::string> applied_patches;

  for (auto&& [suffix, file] : *output_files) {
    if (patches.count(suffix) == 0) {
      continue;
    }
    applied_patches.insert(suffix);
    for (auto&& [position, total_size, suffix_size] : patches.at(suffix)) {
      if (desired_path.size() > total_size) {
        LOG_WARN(
            "Unexpected: We need more space than reserved in the output file.");
        return false;
      }
      LOG_TRACE("Applying patch ({}, {}, {}) on file with extension [{}].",
                position, total_size, suffix_size, suffix);

      std::memcpy(file.data() + position, desired_path.data(),
                  desired_path.size());
      std::memmove(file.data() + position + desired_path.size(),
                   file.data() + position + total_size - suffix_size,
                   suffix_size);

      // Zero out all the rest bytes.
      auto new_size = desired_path.size() + suffix_size;
      memset(file.data() + position + new_size, 0, total_size - new_size);
    }
  }
  for (auto&& [suffix, _] : patches) {
    if (applied_patches.count(suffix) == 0) {
      LOG_WARN(
          "Unexpected: Patches were prepared for file with extension [{}], but "
          "no corresponding file was found.",
          suffix);
      return false;
    }
  }
  return true;
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
  return WaitForCompilationTask(*task_id, args);
}

}  // namespace

std::optional<std::string> SubmitCompilationTask(
    const CompilerArgs& args, RewriteResult rewritten_source) {
  auto&& compiler = args.GetCompiler();
  auto&& [mtime, size] = GetMtimeAndSize(compiler);
  Json::Value submit_task_req;
  submit_task_req["requestor_process_id"] = getpid();
  submit_task_req["source_path"] = rewritten_source.source_path;
  submit_task_req["source_digest"] = rewritten_source.source_digest;
  submit_task_req["compiler_invocation_arguments"] =
      args.Rewrite(kIgnoredArgs, kIgnoredArgPrefixes,
                   {"-fpreprocessed",
                    rewritten_source.directives_only ? "-fdirectives-only" : "",
                    "-x", rewritten_source.language, "-"},
                   false)
          .ToCommandLine(false);
  submit_task_req["cache_control"] =
      static_cast<int>(rewritten_source.cache_control);
  submit_task_req["compiler"]["path"] = compiler;
  submit_task_req["compiler"]["size"] = static_cast<Json::UInt64>(size);
  submit_task_req["compiler"]["timestamp"] = static_cast<Json::UInt64>(mtime);

  std::vector<std::string_view> parts;
  auto req = Json::FastWriter().write(submit_task_req);
  parts.push_back(req);
  parts.push_back(rewritten_source.zstd_rewritten);
  auto header = MakeMultiChunkHeader(parts);
  parts.insert(parts.begin(), header);

  auto result = DaemonCallGathered("/local/submit_cxx_task",
                                   {"Content-Type: application/x-multi-chunk"},
                                   parts, 5s);

  if (result.status == 400) {
    LOG_TRACE(
        "Compiler not recognized by the daemon? Try reporting the compiler.");
    // FIXME: Escaping possible quotes in `path`.
    auto req = fmt::format(
        R"({{"file_desc":{{"path":"{}","size":{},"timestamp":{}}},"digest":"{}"}})",
        compiler, size, mtime, Blake3Digest(compiler));
    auto&& [status, body] = DaemonCall(
        "/local/set_file_digest", {"Content-Type: application/json"}, req, 1s);
    if (status != 200) {
      LOG_ERROR("Failed to report compiler digest to daemonL: [{}] {}", status,
                body);
      return {};
    }

    // Try again.
    result = DaemonCallGathered("/local/submit_cxx_task",
                                {"Content-Type: application/x-multi-chunk"},
                                parts, 5s);
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

CompilationResult WaitForCompilationTask(const std::string& task_id,
                                         const CompilerArgs& args) {
  do {
    auto req_body = fmt::format(
        "{{\"task_id\": \"{}\", \"milliseconds_to_wait\": 10000}}", task_id);
    auto&& [status, body] = DaemonCall(
        "/local/wait_for_cxx_task", {"Content-Type: application/json"},
        req_body, 15s /* Must be greater than `milliseconds_to_wait` */);
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
    auto parsed = TryParseMultiChunk(body);
    Json::Value jsv;
    if (!parsed || parsed->empty() ||
        !Json::Reader().parse(parsed->front().data(),
                              parsed->front().data() + parsed->front().size(),
                              jsv)) {
      LOG_ERROR("Unexpected: Malformed response from delegate daemon.");
      return {-1};
    }

    std::vector<std::pair<std::string, std::string>> output_files;
    std::size_t output_file_bytes = 0;

    for (int i = 0; i != jsv["file_extensions"].size(); ++i) {
      auto path = jsv["file_extensions"][i].asString();
      auto decompressed = DecompressUsingZstd(parsed->at(i + 1));
      output_files.emplace_back(path, std::move(decompressed));
      output_file_bytes += output_files.back().second.size();
    }

    if (args.TryGet("--coverage") || args.TryGet("-ftest-coverage")) {
      std::unordered_map<std::string, PatchLocations> locations;
      for (int i = 0; i != jsv["file_extensions"].size(); ++i) {
        auto&& ext = jsv["file_extensions"][i].asString();
        auto&& patches = jsv["patches"][i]["locations"];
        for (auto&& location : patches) {
          if (!location["position"].isUInt64() ||
              !location["total_size"].isUInt64() ||
              !location["suffix_to_keep"].isUInt64()) {
            LOG_ERROR(
                "Unexpected: Malformed patch locations from delegate daemon.");
            return {-2};
          }
          locations[ext].emplace_back(
              PatchLocation{location["position"].asUInt64(),
                            location["total_size"].asUInt64(),
                            location["suffix_to_keep"].asUInt64()});
        }
      }
      if (!PatchPathOccurrences(&output_files, locations,
                                GetExpectedPath(args))) {
        return {-3};
      }
    }

    CompilationResult result = {jsv["exit_code"].asInt(),
                                jsv["output"].asString(),
                                jsv["error"].asString(), output_files};
    LOG_DEBUG(
        "Compilation result: exit_code {}, stdout {} bytes, stderr {} bytes, "
        "{} output files ({} bytes in total).",
        result.exit_code, result.output.size(), result.error.size(),
        result.output_files.size(), output_file_bytes);
    return result;
  } while (true);
}

CompilationResult CompileOnCloud(const CompilerArgs& args,
                                 RewriteResult rewritten_source) {
  CHECK(!rewritten_source.zstd_rewritten.empty());  // It can't be.
  LOG_TRACE("Preprocessed source code (compressed) is [{}] bytes.",
            rewritten_source.zstd_rewritten.size());
  return CompileOnPublicCloud(args, std::move(rewritten_source));
}

}  // namespace yadcc::client
