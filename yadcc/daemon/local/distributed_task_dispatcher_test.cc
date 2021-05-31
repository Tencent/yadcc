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

#include "yadcc/daemon/local/distributed_task_dispatcher.h"

#include <chrono>

#include "thirdparty/googletest/gtest/gtest.h"

#include "flare/fiber/this_fiber.h"
#include "flare/init/override_flag.h"
#include "flare/testing/main.h"
#include "flare/testing/redis_mock.h"
#include "flare/testing/rpc_mock.h"

#include "yadcc/api/daemon.flare.pb.h"
#include "yadcc/api/scheduler.flare.pb.h"

FLARE_OVERRIDE_FLAG(scheduler_uri, "mock://whatever-it-wants-to-be");
FLARE_OVERRIDE_FLAG(cache_server_uri, "mock://whatever-it-wants-to-be");
FLARE_OVERRIDE_FLAG(debugging_always_use_servant_at, "mock://fake-servant");

using namespace std::literals;

namespace yadcc::daemon::local {

scheduler::WaitForStartingTaskResponse MakeWaitForTaskResponse(int grant_id) {
  scheduler::WaitForStartingTaskResponse result;
  auto ptr = result.add_grants();
  ptr->set_task_grant_id(grant_id);
  ptr->set_servant_location("not-used-as-we've-override-it-via-GFlags");
  return result;
}

daemon::cloud::QueueCompilationTaskResponse MakeQueueCompilationTaskResponse(
    int task_id) {
  daemon::cloud::QueueCompilationTaskResponse resp;
  resp.set_status(daemon::cloud::COMPILATION_TASK_STATUS_RUNNING);
  resp.set_task_id(10);
  return resp;
}

EnvironmentDesc MakeEnvironmentDesc(const std::string& s) {
  EnvironmentDesc desc;
  desc.set_compiler_digest(s);
  return desc;
}

std::atomic<int> keep_alives{};

void KeepTaskAliveHandler(const scheduler::KeepTaskAliveRequest& req,
                          scheduler::KeepTaskAliveResponse* resp,
                          flare::RpcServerController* ctlr) {
  for ([[maybe_unused]] auto&& _ : req.task_grant_ids()) {
    resp->add_statuses(true);
  }
  ++keep_alives;
}

void WaitForCompilationOutputHandler(
    const daemon::cloud::WaitForCompilationOutputRequest& req,
    daemon::cloud::WaitForCompilationOutputResponse* resp,
    flare::RpcServerController* ctlr) {
  static int counter = 0;

  if (++counter == 1) {  // The first call times out.
    flare::this_fiber::SleepFor(2s);
    resp->set_status(daemon::cloud::COMPILATION_TASK_STATUS_RUNNING);
  } else {  // The second one succeeds.
    resp->set_status(daemon::cloud::COMPILATION_TASK_STATUS_DONE);
    resp->set_exit_code(0);
    ctlr->SetResponseAttachment(flare::CreateBufferSlow("my output"));
  }
}

TEST(DistributedTaskDispatcher, All) {
  ///////////////////////////////////////////////////
  // Mocking cache server, we simply let it fail.  //
  ///////////////////////////////////////////////////
  FLARE_EXPECT_REDIS_COMMAND(::testing::_)
      .WillRepeatedly(flare::testing::Return(flare::RedisError{"ERR", "msg"}));

  ///////////////////////////////////
  // Mocking scheduler's methods.  //
  ///////////////////////////////////

  std::atomic<std::size_t> freed_tasks{};
  FLARE_EXPECT_RPC(scheduler::SchedulerService::WaitForStartingTask,
                   ::testing::_)
      .WillRepeatedly(flare::testing::Return(MakeWaitForTaskResponse(1)));
  FLARE_EXPECT_RPC(scheduler::SchedulerService::KeepTaskAlive, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(KeepTaskAliveHandler));
  FLARE_EXPECT_RPC(scheduler::SchedulerService::FreeTask, ::testing::_)
      .WillRepeatedly(
          flare::testing::HandleRpc([&](auto&&...) { ++freed_tasks; }));
  FLARE_EXPECT_RPC(scheduler::SchedulerService::GetConfig, ::testing::_)
      .WillRepeatedly(flare::testing::HandleRpc(
          [&](auto&&, scheduler::GetConfigResponse* resp, auto&&) {
            resp->set_serving_daemon_token("123");
          }));

  /////////////////////////////////
  // Mocking servant's methods.  //
  /////////////////////////////////

  FLARE_EXPECT_RPC(daemon::cloud::DaemonService::QueueCompilationTask,
                   ::testing::_)
      .WillRepeatedly(
          flare::testing::Return(MakeQueueCompilationTaskResponse(1)));
  FLARE_EXPECT_RPC(daemon::cloud::DaemonService::WaitForCompilationOutput,
                   ::testing::_)
      .WillRepeatedly(
          flare::testing::HandleRpc(WaitForCompilationOutputHandler));
  FLARE_EXPECT_RPC(daemon::cloud::DaemonService::FreeTask, ::testing::_)
      .WillRepeatedly(
          flare::testing::Return(daemon::cloud::FreeTaskResponse()));

  //////////////////////
  // UT starts here.  //
  //////////////////////

  auto task_id = DistributedTaskDispatcher::Instance()->QueueTask(
      CompilationTask{.requestor_pid = 1,
                      .env_desc = MakeEnvironmentDesc("compiler-env"),
                      .source_digest = "cache key digest",
                      .preprocessed_source = flare::CreateBufferSlow("buffer")},
      flare::ReadCoarseSteadyClock() + 100s);

  CompilationOutput compilation_output;
  ASSERT_EQ(DistributedTaskDispatcher::WaitStatus::Timeout,
            DistributedTaskDispatcher::Instance()->WaitForTask(
                task_id, 1s, &compilation_output));

  // @sa: First expectation on `WaitForCompilationOutput`.
  std::this_thread::sleep_for(3s);
  ASSERT_EQ(DistributedTaskDispatcher::WaitStatus::OK,
            DistributedTaskDispatcher::Instance()->WaitForTask(
                task_id, 1s, &compilation_output));
  EXPECT_EQ(0, compilation_output.exit_code);
  EXPECT_EQ("my output", flare::FlattenSlow(compilation_output.object_file));

  // The task is dropped on first successful wait.
  ASSERT_EQ(DistributedTaskDispatcher::WaitStatus::NotFound,
            DistributedTaskDispatcher::Instance()->WaitForTask(
                task_id, 1s, &compilation_output));

  EXPECT_GT(keep_alives, 1);
  EXPECT_EQ(1, freed_tasks);
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
