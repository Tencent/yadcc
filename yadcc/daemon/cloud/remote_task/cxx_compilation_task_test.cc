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

#include <string>

#include "gtest/gtest.h"

#include "flare/base/compression.h"
#include "flare/base/crypto/blake3.h"
#include "flare/base/encoding/hex.h"
#include "flare/base/string.h"
#include "flare/testing/hooking_mock.h"
#include "flare/testing/main.h"

#include "yadcc/api/daemon.pb.h"
#include "yadcc/api/extra_info.pb.h"
#include "yadcc/common/io.h"
#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/cloud/compiler_registry.h"
#include "yadcc/daemon/task_digest.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

TEST(CxxCompilationTask, All) {
  FLARE_EXPECT_HOOKED_CALL(CompilerRegistry::Instance)
      .WillRepeatedly(testing::Return(nullptr));
  FLARE_EXPECT_HOOKED_CALL(&CompilerRegistry::TryGetCompilerPath, testing::_,
                           testing::_)
      .WillRepeatedly(testing::Return("/path/to/compiler"));

  QueueCxxCompilationTaskRequest req;
  req.set_invocation_arguments("-fancy-args");
  req.mutable_env_desc()->set_compiler_digest("/path/to/compiler");
  req.set_compression_algorithm(COMPRESSION_ALGORITHM_ZSTD);

  auto src_digest = flare::EncodeHex(flare::Blake3("src"));
  CxxCompilationTask task;
  auto status = task.Prepare(
      req, *flare::Compress(flare::MakeCompressor("zstd").get(), "src"));
  ASSERT_EQ(0, status.code());

  auto file_prefix =
      flare::Format("{}/{}/output", task.workspace_dir_.GetPath(),
                    task.temporary_dir_extra_depth_);
  EXPECT_EQ(flare::Format("/path/to/compiler -fancy-args -o {}.o", file_prefix),
            task.GetCommandLine());
  EXPECT_EQ("src", flare::FlattenSlow(task.GetStandardInputOnce()));
  EXPECT_EQ(
      GetCxxTaskDigest(req.env_desc(), req.invocation_arguments(), src_digest),
      task.GetDigest());

  WriteAll(file_prefix + ".o", flare::CreateBufferSlow("output"));
  WriteAll(file_prefix + ".gcno",
           flare::CreateBufferSlow("output123" + file_prefix + "456\0"s));

  auto oob = task.GetOobOutput(0, "stdout", "stderr");
  ASSERT_TRUE(oob);

  std::map output_files(oob->files.begin(), oob->files.end());
  EXPECT_EQ("output", flare::FlattenSlow(output_files.at(".o")));
  EXPECT_EQ("output123" + file_prefix + "456\0"s,
            flare::FlattenSlow(output_files.at(".gcno")));

  auto cache_key = task.GetCacheKey();
  ASSERT_TRUE(cache_key);
  EXPECT_EQ(GetCxxCacheEntryKey(req.env_desc(), req.invocation_arguments(),
                                src_digest),
            *cache_key);

  CxxCompilationExtraInfo info;
  ASSERT_TRUE(oob->extra_info.UnpackTo(&info));
  EXPECT_EQ(2, info.file_name_patches().size());
  EXPECT_EQ(0, info.file_name_patches().at(".o").locations().size());
  EXPECT_EQ(1, info.file_name_patches().at(".gcno").locations().size());

  auto&& loc = info.file_name_patches().at(".gcno").locations(0);
  EXPECT_EQ(9, loc.position());
  EXPECT_EQ(file_prefix.size() + 3, loc.total_size());
  EXPECT_EQ(3, loc.suffix_to_keep());
}

}  // namespace yadcc::daemon::cloud

FLARE_TEST_MAIN
