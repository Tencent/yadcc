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

#include "yadcc/daemon/cloud/remote_task.h"

#include "flare/base/buffer/packing.h"
#include "flare/base/compression.h"

#include "yadcc/daemon/cloud/distributed_cache_writer.h"

namespace yadcc::daemon::cloud {

namespace {

flare::Compressor* GetZstdCompressor() {
  thread_local auto compressor = flare::MakeCompressor("zstd");
  return compressor.get();
}

}  // namespace

int RemoteTask::GetExitCode() const { return exit_code_; }

const std::string& RemoteTask::GetStandardOutput() const { return stdout_; }

const std::string& RemoteTask::GetStandardError() const { return stderr_; }

const google::protobuf::Any& RemoteTask::GetExtraInfo() const {
  return extra_info_;
}

const flare::NoncontiguousBuffer& RemoteTask::GetOutputFilePack() const {
  return file_pack_;
}

void RemoteTask::OnCompletion(int exit_code,
                              flare::NoncontiguousBuffer standard_output,
                              flare::NoncontiguousBuffer standard_error) {
  exit_code_ = exit_code;
  stdout_ = flare::FlattenSlow(standard_output);
  stderr_ = flare::FlattenSlow(standard_error);

  auto oob = GetOobOutput(exit_code, stdout_, stderr_);
  if (!oob) {
    // Why would it fail with a success error code?
    FLARE_CHECK_NE(oob.error().code(), 0);

    // If post-processing failed, use whatever returned by it as the final
    // result.
    exit_code_ = oob.error().code();
    stdout_.clear();
    stderr_ = oob.error().message();
    return;
  }
  extra_info_ = std::move(oob->extra_info);

  // By our convention, files are compressed with zstd.
  auto&& files = oob->files;
  for (auto&& [k, v] : files) {
    auto compressed = flare::Compress(GetZstdCompressor(), v);
    FLARE_CHECK(compressed);  // How can compress fail?
    v = std::move(*compressed);
  }
  file_pack_ = flare::WriteKeyedNoncontiguousBuffers(files);

  // Let's see if we should fill the cache.
  //
  // Note that for the moment we don't fill the cache if the command fails.
  if (auto key = GetCacheKey(); key && exit_code == 0) {
    CacheEntry entry = {.exit_code = exit_code_,
                        .standard_output = stdout_,
                        .standard_error = stderr_,
                        .extra_info = extra_info_,
                        .files = file_pack_};
    (void)DistributedCacheWriter::Instance()->AsyncWrite(*key, entry);
  }
}

}  // namespace yadcc::daemon::cloud
