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

#include "yadcc/daemon/local/http_service_impl.h"

#include "gtest/gtest.h"
#include "jsoncpp/json.h"

#include "flare/init/override_flag.h"
#include "flare/testing/hooking_mock.h"
#include "flare/testing/main.h"

#include "yadcc/api/extra_info.pb.h"
#include "yadcc/daemon/local/distributed_task_dispatcher.h"
#include "yadcc/daemon/local/file_digest_cache.h"
#include "yadcc/daemon/local/multi_chunk.h"

namespace yadcc::daemon::local {

namespace {

flare::NoncontiguousBuffer MakeFakeSubmitCxxTaskRequest() {
  Json::Value submit_task_req;
  submit_task_req["requestor_process_id"] = 123;
  submit_task_req["source_path"] = "fancy/path/to/1.cxx";
  submit_task_req["source_digest"] = "source digest";
  submit_task_req["compiler_invocation_arguments"] = "-Werror";
  submit_task_req["cache_control"] = 0;
  submit_task_req["compiler"]["path"] = "/fake/path";
  submit_task_req["compiler"]["size"] = 234;
  submit_task_req["compiler"]["timestamp"] = 123;

  return MakeMultiChunk(
      {flare::CreateBufferSlow(Json::FastWriter().write(submit_task_req)),
       flare::CreateBufferSlow("fake src")});
}

}  // namespace

TEST(HttpServiceImpl, Cxx) {
  // Don't instantiate the dispatcher.
  FLARE_EXPECT_HOOKED_CALL(&DistributedTaskDispatcher::Instance)
      .WillRepeatedly([](auto&&...) { return nullptr; });

  FileDigestCache::Instance()->Set("/fake/path", 234, 123, "some digest");

  HttpServiceImpl service;

  flare::HttpRequest request;
  flare::HttpResponse response;
  flare::HttpServerContext context;

  request.set_method(flare::HttpMethod::Post);

  ////////////////////
  // Submit task.   //
  ////////////////////

  std::unique_ptr<DistributedTask> captured_task;

  FLARE_EXPECT_HOOKED_CALL(&DistributedTaskDispatcher::QueueDistributedTask,
                           ::testing::_, ::testing::_, ::testing::_,
                           ::testing::_)
      .WillRepeatedly(
          [&](auto*, auto, std::unique_ptr<DistributedTask> task, auto&&...) {
            captured_task = std::move(task);
            return 100;
          });

  request.set_uri("/local/submit_cxx_task");
  request.set_body(MakeFakeSubmitCxxTaskRequest());
  service.HandleRequest(request, &response, &context);

  EXPECT_EQ(flare::HttpStatus::OK, response.status()) << *response.body();
  Json::Value resp_jsv;
  ASSERT_TRUE(Json::Reader().parse(*response.body(), resp_jsv));
  EXPECT_EQ("100", resp_jsv["task_id"].asString());

  ///////////////////////////
  // Wait for completion.  //
  ///////////////////////////

  auto handler = [&](const DistributedTaskDispatcher*, flare::TypeIndex type,
                     std::uint64_t task_id, std::chrono::nanoseconds timeout) {
    DistributedTaskOutput output = {
        .exit_code = 0,
        .standard_output = "output",
        .standard_error = "error",
        .output_files = {{".o", flare::CreateBufferSlow("cxx output o")}}};

    CxxCompilationExtraInfo extra;

    auto extra_location =
        (*extra.mutable_file_name_patches())[".o"].add_locations();
    extra_location->set_position(1);
    extra_location->set_total_size(2);
    extra_location->set_suffix_to_keep(3);
    extra_location = (*extra.mutable_file_name_patches())[".o"].add_locations();
    extra_location->set_position(4);
    extra_location->set_total_size(5);
    extra_location->set_suffix_to_keep(6);

    output.extra_info.PackFrom(extra);

    captured_task->OnCompletion(output);
    return std::move(captured_task);
  };

  FLARE_EXPECT_HOOKED_CALL(&DistributedTaskDispatcher::WaitForDistributedTask,
                           ::testing::_, ::testing::_, ::testing::_,
                           ::testing::_)
      .WillRepeatedly(testing::Invoke(handler));

  request.set_uri("/local/wait_for_cxx_task");
  request.set_body(R"({"task_id":"10","milliseconds_to_wait":1000})");
  service.HandleRequest(request, &response, &context);

  EXPECT_EQ(flare::HttpStatus::OK, response.status()) << *response.body();
  auto chunks = TryParseMultiChunk(*response.noncontiguous_body());
  ASSERT_TRUE(chunks);
  ASSERT_EQ(2, chunks->size());
  ASSERT_TRUE(
      Json::Reader().parse(flare::FlattenSlow(chunks->at(0)), resp_jsv));
  EXPECT_EQ(0, resp_jsv["exit_code"].asInt());
  EXPECT_EQ("output", resp_jsv["output"].asString());
  EXPECT_EQ("error", resp_jsv["error"].asString());

  EXPECT_EQ(".o", resp_jsv["file_extensions"][0].asString());
  auto& locations = resp_jsv["patches"][0]["locations"];
  EXPECT_EQ(1, locations[0]["position"].asInt());
  EXPECT_EQ(2, locations[0]["total_size"].asInt());
  EXPECT_EQ(3, locations[0]["suffix_to_keep"].asInt());
  EXPECT_EQ(4, locations[1]["position"].asInt());
  EXPECT_EQ(5, locations[1]["total_size"].asInt());
  EXPECT_EQ(6, locations[1]["suffix_to_keep"].asInt());
  EXPECT_EQ("cxx output o", flare::FlattenSlow(chunks->at(1)));
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
