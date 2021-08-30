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

#include "yadcc/daemon/local/distributed_task/cxx_compilation_task.h"

#include "gtest/gtest.h"

#include "flare/base/buffer/packing.h"
#include "flare/testing/hooking_mock.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_mock.h"

#include "yadcc/api/daemon.pb.h"
#include "yadcc/api/extra_info.pb.h"
#include "yadcc/common/io.h"
#include "yadcc/daemon/local/file_digest_cache.h"
#include "yadcc/daemon/local/messages.pb.h"

using namespace std::literals;

namespace yadcc::daemon::local {

std::unique_ptr<CxxCompilationTask> MakeTask() {
  SubmitCxxTaskRequest req;
  req.set_requestor_process_id(1234);
  req.set_source_path("path/to/src.cc");
  req.set_source_digest("some digest");
  req.set_compiler_invocation_arguments("-Werror");
  req.set_cache_control(
      static_cast<::google::protobuf::int32>(CacheControl::Disallow));
  req.mutable_compiler()->set_path("/path/to/compiler");
  req.mutable_compiler()->set_size(123);
  req.mutable_compiler()->set_timestamp(456);
  std::vector bytes{flare::CreateBufferSlow("source")};

  auto result = std::make_unique<CxxCompilationTask>();
  auto status = result->Prepare(req, bytes);
  FLARE_CHECK(status.ok(), "Failed to initialize: {}", status.ToString());
  return result;
}

TEST(CxxCompilationTask, SubmitSuccess) {
  FileDigestCache::Instance()->Set("/path/to/compiler", 123, 456, "my env");
  auto handler = [&](const cloud::QueueCxxCompilationTaskRequest& req,
                     cloud::QueueCxxCompilationTaskResponse* resp,
                     flare::RpcServerController* ctlr) {
    EXPECT_EQ("my token", req.token());
    EXPECT_EQ(1234, req.task_grant_id());
    EXPECT_EQ("my env", req.env_desc().compiler_digest());
    EXPECT_EQ("path/to/src.cc", req.source_path());
    EXPECT_EQ("-Werror", req.invocation_arguments());
    EXPECT_TRUE(req.disallow_cache_fill());
    EXPECT_EQ("source", flare::FlattenSlow(ctlr->GetRequestAttachment()));
    resp->set_task_id(12345);
  };
  FLARE_EXPECT_RPC(cloud::DaemonService::QueueCxxCompilationTask, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(handler));

  cloud::DaemonService_SyncStub stub("mock://whatever-it-wants-to-be");
  auto task = MakeTask();
  auto result = task->StartTask("my token", 1234, &stub);
  ASSERT_TRUE(result);
  EXPECT_EQ(12345, *result);

  DistributedTaskOutput output;
  output.exit_code = 0;
  output.output_files = {{".o", flare::CreateBufferSlow("cxx output o")}};
  CxxCompilationExtraInfo extra;
  auto extra_location =
      (*extra.mutable_file_name_patches())[".o"].add_locations();
  extra_location->set_position(1);
  extra_location->set_total_size(2);
  extra_location->set_suffix_to_keep(3);
  output.extra_info.PackFrom(extra);
  static_cast<DistributedTask*>(task.get())->OnCompletion(output);
  auto task_output = task->GetOutput();
  EXPECT_TRUE(!!task_output);
  auto rsp = task_output->first;
  EXPECT_EQ(0, rsp.exit_code());
  EXPECT_EQ(".o", rsp.file_extensions(0));
  EXPECT_EQ(1, rsp.patches(0).locations(0).position());
  EXPECT_EQ(2, rsp.patches(0).locations(0).total_size());
  EXPECT_EQ(3, rsp.patches(0).locations(0).suffix_to_keep());
  std::vector<std::string> output_files;
  for (auto&& b : task_output->second) {
    output_files.emplace_back(flare::FlattenSlow(b));
  }
  EXPECT_THAT(output_files, ::testing::UnorderedElementsAre("cxx output o"));
}

TEST(CxxCompilationTask, SubmitFailure) {
  FLARE_EXPECT_RPC(cloud::DaemonService::QueueCxxCompilationTask, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(
          [&](auto&&, cloud::QueueCxxCompilationTaskResponse* resp,
              flare::RpcServerController* ctlr) {
            ctlr->SetFailed(cloud::STATUS_ACCESS_DENIED, "Access denied");
          }));

  cloud::DaemonService_SyncStub stub("mock://whatever-it-wants-to-be");
  auto task = MakeTask();
  auto result = task->StartTask("my token", 1234, &stub);
  ASSERT_FALSE(result);
  EXPECT_EQ(cloud::STATUS_ACCESS_DENIED, result.error().code());
  EXPECT_EQ("Access denied", result.error().message());
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
