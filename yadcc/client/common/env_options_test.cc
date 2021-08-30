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

#include "yadcc/client/common/env_options.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace yadcc::client {

TEST(EnvOptions, All) {
  setenv("YADCC_COMPILE_ON_CLOUD_SIZE_THRESHOLD", "1234", 1);
  EXPECT_EQ(1234, GetOptionCompileOnCloudSizeThreshold());

  setenv("YADCC_WARN_ON_WAIT_LONGER_THAN", "123", 1);
  EXPECT_EQ(123, GetOptionWarnOnWaitLongerThan());

  setenv("YADCC_LOG_LEVEL", "5", 1);
  EXPECT_EQ(5, GetOptionLogLevel());

  setenv("YADCC_CACHE_CONTROL", "0", 1);
  EXPECT_EQ(CacheControl::Disallow, GetOptionCacheControl());

  setenv("YADCC_DAEMON_PORT", "1234", 1);
  EXPECT_EQ(1234, GetOptionDaemonPort());

  setenv("YADCC_IGNORE_TIMESTAMP_MACROS", "1", 1);
  EXPECT_TRUE(GetOptionIgnoreTimestampMacros());

  setenv("YADCC_TREAT_SOURCE_FROM_STDIN_AS_LIGHTWEIGHT", "1", 1);
  EXPECT_TRUE(GetOptionTreatSourceFromStdinAsLightweight());

  setenv("YADCC_WARN_ON_NONCACHEABLE", "1", 1);
  EXPECT_TRUE(GetOptionWarnOnNoncacheble());

  setenv("YADCC_DEBUGGING_COMPILE_LOCALLY", "1", 1);
  EXPECT_TRUE(GetOptionDebuggingCompileLocally());
}

}  // namespace yadcc::client
