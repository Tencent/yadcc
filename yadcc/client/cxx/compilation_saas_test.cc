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

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"
#include "jsoncpp/json.h"

#include "yadcc/client/common/compress.h"
#include "yadcc/client/common/daemon_call.h"
#include "yadcc/client/common/logging.h"
#include "yadcc/client/common/multi_chunk.h"

namespace yadcc::client {

TEST(CompilationSaas, SubmitTask) {
  Json::Value json_req;
  std::vector<std::string> parts;

  SetDaemonCallGatheredHandler([&](auto&& api, auto&& headers, auto&& bodies,
                                   auto&& timeout) {
    std::string joined;
    for (auto&& e : bodies) {
      joined += e;
    }

    if (api == "/local/submit_cxx_task") {
      auto parsed = TryParseMultiChunk(joined);
      CHECK(parsed);
      CHECK(Json::Reader().parse(parsed->at(0).data(),
                                 parsed->at(0).data() + parsed->at(0).size(),
                                 json_req));
      parts.assign(parsed->begin() + 1, parsed->end());
      return DaemonResponse{200, R"({"task_id":"1234"})"};
    } else {
      CHECK("Can't be here.");
    }
    return DaemonResponse{404};
  });

  const char* argvs[] = {"-c", "-std=c++11", "-o", "1.o", "1.cc"};
  CompilerArgs args(std::size(argvs), argvs);
  args.SetCompiler("testdata/fake-g++");
  RewriteResult rewritten_source;
  rewritten_source.source_path = "/source/path";
  rewritten_source.source_digest = "digest";
  rewritten_source.cache_control = CacheControl::Disallow;
  rewritten_source.zstd_rewritten = "zstd_rewritten";

  auto task_id = SubmitCompilationTask(args, rewritten_source);
  ASSERT_TRUE(task_id);
  EXPECT_EQ("1234", *task_id);

  EXPECT_EQ("/source/path", json_req["source_path"].asString());
  EXPECT_EQ("digest", json_req["source_digest"].asString());
  EXPECT_THAT(parts, ::testing::UnorderedElementsAre("zstd_rewritten"));
}

TEST(CompilationSaas, WaitForTask) {
  SetDaemonCallGatheredHandler(
      [&](auto&& api, auto&& headers, auto&& bodies, auto&& timeout) {
        CHECK(api == "/local/wait_for_cxx_task");
        auto resp_msg =
            "{"
            R"("exit_code":0,"output":"stdout","error":"stderr",)"
            R"("file_extensions":[".o",".gcno"])"
            "}";
        std::string resp =
            MakeMultiChunk({resp_msg, CompressUsingZstd("o format output"),
                            CompressUsingZstd("gcno format output")});
        return DaemonResponse{200, resp};
      });
  const char* argvs[] = {"-c", "-std=c++11", "-o", "1.o", "1.cc"};
  CompilerArgs args(std::size(argvs), argvs);
  auto result = WaitForCompilationTask("123", args);
  // ASSERT_TRUE(result);
  EXPECT_EQ(0, result.exit_code);
  EXPECT_EQ("stdout", result.output);
  EXPECT_EQ("stderr", result.error);
  ASSERT_EQ(2, result.output_files.size());
  EXPECT_THAT(result.output_files,
              ::testing::UnorderedElementsAre(
                  std::pair(".o", "o format output"),
                  std::pair(".gcno", "gcno format output")));
}

}  // namespace yadcc::client
