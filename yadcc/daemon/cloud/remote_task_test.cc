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

#include "gtest/gtest.h"

#include "flare/base/buffer/packing.h"
#include "flare/base/compression.h"
#include "flare/rpc/rpc_server_controller.h"
#include "flare/testing/hooking_mock.h"

#include "yadcc/daemon/cloud/distributed_cache_writer.h"

namespace yadcc::daemon::cloud {

namespace {

class FancyRemoteTask : public RemoteTask {
 public:
  std::string GetCommandLine() const override { return ""; }
  flare::NoncontiguousBuffer GetStandardInputOnce() override { return {}; }
  Json::Value DumpInternals() const override { return {}; }
  std::string GetDigest() const override { return ""; }
  std::optional<std::string> GetCacheKey() const override { return ""; }

 protected:
  flare::Expected<OobOutput, flare::Status> GetOobOutput(
      int exit_code, const std::string& standard_output,
      const std::string& standard_error) override {
    OobOutput output;
    output.extra_info.set_type_url("something");
    output.files = {{"k1", flare::CreateBufferSlow("v1")},
                    {"k2", flare::CreateBufferSlow("v2")}};
    return output;
  }
};

class ErrorRemoteTask : public RemoteTask {
 public:
  std::string GetCommandLine() const override { return ""; }
  flare::NoncontiguousBuffer GetStandardInputOnce() override { return {}; }
  Json::Value DumpInternals() const override { return {}; }
  std::string GetDigest() const override { return ""; }
  std::optional<std::string> GetCacheKey() const override { return ""; }

 protected:
  flare::Expected<OobOutput, flare::Status> GetOobOutput(
      int exit_code, const std::string& standard_output,
      const std::string& standard_error) override {
    return flare::Status{-123, "Something goes wrong."};
  }
};

std::string DecompressUsingZstd(const flare::NoncontiguousBuffer& buffer) {
  return flare::FlattenSlow(
      *flare::Decompress(flare::MakeDecompressor("zstd").get(), buffer));
}

}  // namespace

TEST(RemoteTask, Success) {
  FLARE_EXPECT_HOOKED_CALL(DistributedCacheWriter::Instance)
      .WillRepeatedly(testing::Return(nullptr));
  bool cache_written = false;
  FLARE_EXPECT_HOOKED_CALL(&DistributedCacheWriter::AsyncWrite, ::testing::_,
                           ::testing::_, ::testing::_)
      .WillRepeatedly(testing::Invoke(
          [&](auto, const std::string& key, const CacheEntry& cache_entry) {
            cache_written = true;
            EXPECT_EQ(0, cache_entry.exit_code);
            EXPECT_EQ("stdout", cache_entry.standard_output);
            EXPECT_EQ("stderr", cache_entry.standard_error);
            return true;
          }));

  FancyRemoteTask task;

  static_cast<ExecutionTask*>(&task)->OnCompletion(
      0, flare::CreateBufferSlow("stdout"), flare::CreateBufferSlow("stderr"));
  EXPECT_EQ(0, task.GetExitCode());
  EXPECT_EQ("stdout", task.GetStandardOutput());
  EXPECT_EQ("stderr", task.GetStandardError());
  EXPECT_EQ("something", task.GetExtraInfo().type_url());
  auto parsed =
      flare::TryParseKeyedNoncontiguousBuffers(task.GetOutputFilePack());
  ASSERT_TRUE(parsed);
  ASSERT_EQ(2, parsed->size());
  EXPECT_EQ("k1", parsed->at(0).first);
  EXPECT_EQ("k2", parsed->at(1).first);
  EXPECT_EQ("v1", DecompressUsingZstd(parsed->at(0).second));
  EXPECT_EQ("v2", DecompressUsingZstd(parsed->at(1).second));
}

TEST(RemoteTask, Failure) {
  ErrorRemoteTask task;
  static_cast<ExecutionTask*>(&task)->OnCompletion(
      10, flare::CreateBufferSlow("stdout"), flare::CreateBufferSlow("stderr"));

  EXPECT_EQ(-123, task.GetExitCode());
  EXPECT_EQ("", task.GetStandardOutput());
  EXPECT_EQ("Something goes wrong.", task.GetStandardError());
  EXPECT_TRUE(task.GetExtraInfo().type_url().empty());
  EXPECT_TRUE(task.GetOutputFilePack().Empty());
}

}  // namespace yadcc::daemon::cloud
