// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "yadcc/daemon/cloud/remote_task/cxx_compilation_task.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "flare/base/buffer/view.h"
#include "flare/base/compression.h"
#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding/hex.h"
#include "flare/base/status.h"
#include "flare/base/string.h"
#include "flare/fiber/async.h"
#include "flare/fiber/future.h"

#include "yadcc/api/extra_info.pb.h"
#include "yadcc/common/dir.h"
#include "yadcc/common/io.h"
#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/cloud/compiler_registry.h"
#include "yadcc/daemon/task_digest.h"
#include "yadcc/daemon/temp_dir.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

namespace {

// Determines if the compilation task is cacheable.
flare::Future<bool> VerifyTaskCachability(
    bool allow_cache, const std::string_view& arguments,
    const flare::NoncontiguousBuffer& buffer) {
  static constexpr std::string_view kMacros[] = {"__TIME__", "__DATE__",
                                                 "__TIMESTAMP__"};

  if (!allow_cache) {
    return false;  // As obvious.
  }

  // If all of the macros are overridden, it's always cacheable.
  if (std::all_of(std::begin(kMacros), std::end(kMacros), [&](auto&& e) {
        return arguments.find(flare::Format("-D{}=", e)) !=
               std::string_view::npos;
      })) {
    return true;
  }

  // This scan can be slow. Moving it out of critical path improves
  // responsiveness.
  return flare::fiber::Async([buffer] {
    flare::NoncontiguousBufferForwardView view(buffer);
    for (auto&& e : kMacros) {
      if (std::search(view.begin(), view.end(), e.begin(), e.end()) !=
          view.end()) {
        return false;
      }
    }
    return true;
  });
}

std::string MakeLongLongRelativePathWith(const std::string& base_path,
                                         const std::string& salt) {
  // Leaving 30 bytes for user to use.
  static constexpr auto kMaxPath = PATH_MAX - 30;
  auto result = flare::Format("{}/{}/", base_path, salt);
  while (result.size() + 1 /* The backslash */ < kMaxPath) {
    auto subdir_length =
        std::min<std::size_t>(NAME_MAX, kMaxPath - result.size() - 1);
    result += std::string(subdir_length, 'A') + "/";
  }
  FLARE_CHECK_EQ(result.back(), '/');
  result.pop_back();
  Mkdirs(result);
  return result.substr(base_path.size() + 1 /* The backslash */);
}

patch::Locations FindAllPathLocation(const flare::NoncontiguousBuffer& buffer,
                                     const std::string& prefix) {
  constexpr auto kTerminateNull = "\0"sv;

  FLARE_CHECK(!prefix.empty());
  patch::Locations locations;
  flare::NoncontiguousBufferRandomView view(buffer);
  auto start = view.begin();

#ifndef NDEBUG  // TESTING CODE: Tests if Flare's random view works as intended.
  auto flattened = flare::FlattenSlow(buffer);
#endif

  while (true) {
#ifndef NDEBUG
    auto expected_pos = flattened.find(prefix, start - view.begin());
#endif
    auto pos = std::search(start, view.end(), prefix.begin(), prefix.end());
    if (pos == view.end()) {
#ifndef NDEBUG
      FLARE_CHECK_EQ(expected_pos, std::string::npos);
#endif
      break;
    }

#ifndef NDEBUG
    FLARE_CHECK_EQ(expected_pos, pos - view.begin());
#endif

    auto end = std::search(pos, view.end(), kTerminateNull.begin(),
                           kTerminateNull.end());
    if (end == view.end() || end - pos > PATH_MAX) {
      FLARE_LOG_WARNING(
          "Unexpected: Our path prefix does match, yet it's not a "
          "null-terminated string. Skipping.");
      start = end;
      continue;
    }

    auto&& added = locations.add_locations();
    added->set_position(pos - view.begin());
    added->set_total_size(end - pos);
    added->set_suffix_to_keep(end - pos - prefix.size());
    start = end;
  }
  return locations;
}

flare::Decompressor* GetZstdDecompressor() {
  thread_local auto decompressor = flare::MakeDecompressor("zstd");
  return decompressor.get();
}

}  // namespace

CxxCompilationTask::CxxCompilationTask() : workspace_dir_(GetTemporaryDir()) {}

flare::Status CxxCompilationTask::Prepare(
    const QueueCxxCompilationTaskRequest& request,
    const flare::NoncontiguousBuffer& attachment) {
  // Let's figure out which compiler should be used.
  auto compiler =
      CompilerRegistry::Instance()->TryGetCompilerPath(request.env_desc());
  if (!compiler) {
    return flare::Status(
        STATUS_ENVIRONMENT_NOT_AVAILABLE,
        "The requested environment is not available at this server.");
  }

  if (request.compression_algorithm() != COMPRESSION_ALGORITHM_ZSTD) {
    return flare::Status(STATUS_INVALID_ARGUMENT,
                         "Compression algorithm not supported.");
  }

  auto decompressed_source =
      flare::Decompress(GetZstdDecompressor(), attachment);
  if (!decompressed_source) {
    return flare::Status(STATUS_INVALID_ARGUMENT,
                         "Failed to decompress source code.");
  }

  // Save the request for later use.
  source_ = std::move(*decompressed_source);
  env_desc_ = request.env_desc();
  source_path_ = request.source_path();
  invocation_arguments_ = request.invocation_arguments();
  source_digest_ = flare::EncodeHex(flare::Blake3(source_));
  write_cache_future_ = VerifyTaskCachability(!request.disallow_cache_fill(),
                                              invocation_arguments_, source_);
  temporary_dir_extra_depth_ =
      MakeLongLongRelativePathWith(workspace_dir_.GetPath(), source_digest_);

  // TODO(luobogao): Do some basic security checks on the invocation arguments
  // to prevent threats such as data leak (by specifying path to a sensitive
  // file as source input) etc.
  //
  // Or can we use some jail facility to accomplish this?
  command_line_ = flare::Format(
      "{} {} -o {}/{}/output.o", *compiler, request.invocation_arguments(),
      workspace_dir_.GetPath(), temporary_dir_extra_depth_);
  return flare::Status(0);
}

std::string CxxCompilationTask::GetCommandLine() const { return command_line_; }

flare::NoncontiguousBuffer CxxCompilationTask::GetStandardInputOnce() {
  return std::move(source_);
}

Json::Value CxxCompilationTask::DumpInternals() const {
  Json::Value value;
  value["env"] = env_desc_.compiler_digest();
  value["source_path"] = source_path_;
  value["invocation_arguments"] = invocation_arguments_;
  value["source_digest"] = source_digest_;
  return value;
}

std::string CxxCompilationTask::GetDigest() const {
  return GetCxxTaskDigest(env_desc_, invocation_arguments_, source_digest_);
}

std::optional<std::string> CxxCompilationTask::GetCacheKey() const {
  if (!write_cache_) {
    return std::nullopt;
  }
  return GetCxxCacheEntryKey(env_desc_, invocation_arguments_, source_digest_);
}

flare::Expected<CxxCompilationTask::OobOutput, flare::Status>
CxxCompilationTask::GetOobOutput(int exit_code,
                                 const std::string& standard_output,
                                 const std::string& standard_error) {
  // `write_cache` is likely to be ready now, therefore, instead of chaining a
  // continuation (and makeing the calling convention of `GetOobOutput` weird),
  // we wait on it.
  write_cache_ = flare::fiber::BlockingGet(&write_cache_future_);

  OobOutput result;
  if (exit_code != 0) {
    // If the command fails, we don't care about the output files.
    return result;
  }

  // Read all files generated by the compiler.
  auto output_files = workspace_dir_.ReadAll();

  // Locate file name occurrences in the resulting files. Later on we might
  // want to patch these names.
  CxxCompilationExtraInfo comp_info;
  auto relative_path_prefix = temporary_dir_extra_depth_ + "/output";
  for (auto&& [name, file] : output_files) {
    FLARE_LOG_FATAL_IF(!flare::StartsWith(name, relative_path_prefix),
                       "File [{}] is found unexpectedly.");
    auto suffix = name.substr(relative_path_prefix.size());
    auto locations = FindAllPathLocation(
        file,
        flare::Format("{}/{}", workspace_dir_.GetPath(), relative_path_prefix));
    (*comp_info.mutable_file_name_patches())[suffix] = locations;
  }
  result.extra_info.PackFrom(comp_info);

  // Pack the resulting files.
  auto path_prefix = temporary_dir_extra_depth_ + "/output";
  for (auto&& [name, file] : output_files) {
    FLARE_CHECK(flare::StartsWith(name, path_prefix));
    auto suffix = name.substr(path_prefix.size());
    result.files.emplace_back(std::move(suffix), std::move(file));
  }
  return result;
}

}  // namespace yadcc::daemon::cloud
