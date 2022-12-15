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
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#include "flare/base/buffer/packing.h"
#include "flare/fiber/this_fiber.h"
#include "flare/init/override_flag.h"
#include "flare/testing/main.h"
#include "flare/testing/rpc_mock.h"

#include "yadcc/api/daemon.pb.h"
#include "yadcc/api/scheduler.pb.h"
#include "yadcc/daemon/task_digest.h"

FLARE_OVERRIDE_FLAG(scheduler_uri, "mock://whatever-it-wants-to-be");
FLARE_OVERRIDE_FLAG(cache_server_uri, "mock://whatever-it-wants-to-be");
FLARE_OVERRIDE_FLAG(debugging_always_use_servant_at, "mock://fake-servant");

using namespace std::literals;

namespace yadcc::daemon::local {

namespace {

class TestingTask : public DistributedTask {
 public:
  pid_t GetInvokerPid() const override { return pid; }

  CacheControl GetCacheSetting() const override {
    return CacheControl::Disallow;
  }
  std::string GetCacheKey() const override { return cache_key; }
  std::string GetDigest() const override { return digest; }
  const EnvironmentDesc& GetEnvironmentDesc() const override {
    static const EnvironmentDesc env;
    return env;
  }

  flare::Expected<std::uint64_t, flare::Status> StartTask(
      const std::string& token, std::uint64_t grant_id,
      cloud::DaemonService_SyncStub* stub) override {
    return 10;
  }
  void OnCompletion(const DistributedTaskOutput& output) override {
    this->output = output;
  }

  Json::Value Dump() const override { return {}; }

  pid_t pid;
  std::string cache_key;
  std::string digest;

  DistributedTaskOutput output;
};

}  // namespace

scheduler::WaitForStartingTaskResponse MakeWaitForTaskResponse(int grant_id) {
  scheduler::WaitForStartingTaskResponse result;
  auto ptr = result.add_grants();
  ptr->set_task_grant_id(grant_id);
  ptr->set_servant_location("not-used-as-we've-override-it-via-GFlags");
  return result;
}

EnvironmentDesc MakeEnvironmentDesc(const std::string& s) {
  EnvironmentDesc desc;
  desc.set_compiler_digest(s);
  return desc;
}

scheduler::GetRunningTasksResponse MakeGetRunningTasksResponse() {
  scheduler::GetRunningTasksResponse resp;
  auto running_task = resp.add_running_tasks();
  running_task->set_task_grant_id(78787878);
  running_task->set_servant_task_id(88888888);
  running_task->set_servant_location("repeated servant location");
  running_task->set_task_digest("digest2");
  return resp;
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
  if (req.task_id() == 88888888) {
    resp->set_status(daemon::cloud::COMPILATION_TASK_STATUS_DONE);
    resp->set_exit_code(0);

    std::vector<std::pair<std::string, flare::NoncontiguousBuffer>>
        file_with_suffix{
            {".o", flare::CreateBufferSlow("my repeated output")},
            {".gcno", flare::CreateBufferSlow("my repeated gcno")}};
    ctlr->SetResponseAttachment(
        WriteKeyedNoncontiguousBuffers(file_with_suffix));
  } else {
    if (++counter == 1) {  // The first call times out.
      flare::this_fiber::SleepFor(2s);
      resp->set_status(daemon::cloud::COMPILATION_TASK_STATUS_RUNNING);
    } else {  // The second one succeeds.
      resp->set_status(daemon::cloud::COMPILATION_TASK_STATUS_DONE);
      resp->set_exit_code(0);

      std::vector<std::pair<std::string, flare::NoncontiguousBuffer>>
          file_with_suffix{{".o", flare::CreateBufferSlow("my output")},
                           {".gcno", flare::CreateBufferSlow("my gcno")}};
      ctlr->SetResponseAttachment(
          WriteKeyedNoncontiguousBuffers(file_with_suffix));
    }
  }
}

void WaitForCompilationOutputCompilationFailureHandler(
    const daemon::cloud::WaitForCompilationOutputRequest& req,
    daemon::cloud::WaitForCompilationOutputResponse* resp,
    flare::RpcServerController* ctlr) {
  resp->set_status(daemon::cloud::COMPILATION_TASK_STATUS_DONE);
  resp->set_exit_code(12);
  resp->set_output("output");
  resp->set_error("error");
}

std::unique_ptr<TestingTask> MakeTestingTask(pid_t pid,
                                             const std::string& task_digest,
                                             const std::string& cache_key) {
  auto result = std::make_unique<TestingTask>();
  result->pid = pid;
  result->digest = task_digest;
  result->cache_key = cache_key;
  return result;
}

TEST(DistributedTaskDispatcher, All) {
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
  FLARE_EXPECT_RPC(scheduler::SchedulerService::GetRunningTasks, ::testing::_)
      .WillRepeatedly(flare::testing::Return(MakeGetRunningTasksResponse()));

  /////////////////////////////////
  // Mocking servant's methods.  //
  /////////////////////////////////

  FLARE_EXPECT_RPC(daemon::cloud::DaemonService::WaitForCompilationOutput,
                   ::testing::_)
      .WillOnce(flare::testing::HandleRpc(WaitForCompilationOutputHandler))
      .WillOnce(flare::testing::HandleRpc(WaitForCompilationOutputHandler))
      .WillOnce(flare::testing::HandleRpc(WaitForCompilationOutputHandler))
      .WillRepeatedly(flare::testing::HandleRpc(
          WaitForCompilationOutputCompilationFailureHandler));
  FLARE_EXPECT_RPC(daemon::cloud::DaemonService::FreeTask, ::testing::_)
      .WillRepeatedly(
          flare::testing::Return(daemon::cloud::FreeTaskResponse()));
  FLARE_EXPECT_RPC(daemon::cloud::DaemonService::ReferenceTask, ::testing::_)
      .WillRepeatedly(
          flare::testing::Return(daemon::cloud::ReferenceTaskResponse()));

  //////////////////////
  // UT starts here.  //
  //////////////////////

  // A successful compilation task.

  {
    auto task_id = DistributedTaskDispatcher::Instance()->QueueTask(
        MakeTestingTask(1, "digest1", "cachekey1"),
        flare::ReadCoarseSteadyClock() + 100s);

    ASSERT_EQ(DistributedTaskDispatcher::WaitStatus::Timeout,
              DistributedTaskDispatcher::Instance()
                  ->WaitForTask<TestingTask>(task_id, 1s)
                  .error());

    // @sa: First expectation on `WaitForCompilationOutput`.
    std::this_thread::sleep_for(3s);
    auto wait_result =
        DistributedTaskDispatcher::Instance()->WaitForTask<TestingTask>(task_id,
                                                                        1s);
    ASSERT_TRUE(wait_result);
    auto&& compilation_output =
        static_cast<TestingTask*>(wait_result->get())->output;
    EXPECT_EQ(0, compilation_output.exit_code);
    EXPECT_EQ("my output",
              flare::FlattenSlow(compilation_output.output_files[0].second));
    EXPECT_EQ("my gcno",
              flare::FlattenSlow(compilation_output.output_files[1].second));
    // The task is dropped on first successful wait.
    ASSERT_EQ(DistributedTaskDispatcher::WaitStatus::NotFound,
              DistributedTaskDispatcher::Instance()
                  ->WaitForTask<TestingTask>(task_id, 1s)
                  .error());
  }

  // A repeated compilation task.
  {
    auto task_id = DistributedTaskDispatcher::Instance()->QueueTask(
        MakeTestingTask(1, "digest2", "cachekey2"),
        flare::ReadCoarseSteadyClock() + 100s);

    auto wait_result =
        DistributedTaskDispatcher::Instance()->WaitForTask<TestingTask>(task_id,
                                                                        1s);
    auto&& compilation_output =
        static_cast<TestingTask*>(wait_result->get())->output;
    EXPECT_EQ(0, compilation_output.exit_code);
    EXPECT_EQ("my repeated output",
              flare::FlattenSlow(compilation_output.output_files[0].second));
    EXPECT_EQ("my repeated gcno",
              flare::FlattenSlow(compilation_output.output_files[1].second));
    // The task is dropped on first successful wait.
    ASSERT_EQ(DistributedTaskDispatcher::WaitStatus::NotFound,
              DistributedTaskDispatcher::Instance()
                  ->WaitForTask<TestingTask>(task_id, 1s)
                  .error());
    EXPECT_GT(keep_alives, 1);
  }

  // A failed compilation task.
  {
    auto task_id = DistributedTaskDispatcher::Instance()->QueueTask(
        MakeTestingTask(1, "digest3", "cachekey3"),
        flare::ReadCoarseSteadyClock() + 100s);

    auto wait_result =
        DistributedTaskDispatcher::Instance()->WaitForTask<TestingTask>(task_id,
                                                                        1s);
    auto&& compilation_output =
        static_cast<TestingTask*>(wait_result->get())->output;

    EXPECT_EQ(12, compilation_output.exit_code);
    EXPECT_EQ("output", compilation_output.standard_output);
    EXPECT_EQ("error", compilation_output.standard_error);
  }
}

}  // namespace yadcc::daemon::local

FLARE_TEST_MAIN
