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

#include "yadcc/daemon/common_flags.h"

#include "thirdparty/gflags/gflags.h"

// For the moment the URI below MUST RESOLVES TO A SINGLE HOST. Our scheduler
// does not support cluster configuration for now, running multiple schedulers
// will likely lead to a disaster.
//
// This will be fixed once we implemented HA configuration of scheduler.
DEFINE_string(scheduler_uri, "flare://polaris.or.ip.address.to.scheduler",
              "URI of yadcc scheduler. It MUST RESOLVES TO A SINGLE HOST "
              "(instead of a cluster). For the moment our scheduler does not "
              "support cluster configuration. This issue will be addressed "
              "in the future.");

DEFINE_string(cache_server_uri, "",
              "If set, the daemon will use server here to save (when acting as "
              "a compile-server) and load (when acting as delegate daemon) "
              "compilation result.");

// I don't see much point in separate token for scheduler and token for cache..
// (Unless we want to separate tokens for cache-reader and cache-writer.)
DEFINE_string(token, "",
              "This token is used for accessing scheduler and cache server.");

DEFINE_string(temporary_dir, "/dev/shm",
              "For performance reasons, by default we store temporary files in "
              "`/dev/shm`. If your machine can't afford this, you can specify "
              "a different directory here.");

namespace yadcc::daemon {

// Version 1: First version support auto-upgrade.
// Version 2: Use our homemade cache server (replacing Redis.).
// Version 3: Bloom filter hash generation algorithm changed.
// Version 4: Enable authentication.
// Version 5: Support multiple version of compilers.
// Version 6: Fixing BUG in version 4.
// Version 7: Allow disable caching completely.
int version_for_upgrade = 7;

}  // namespace yadcc::daemon
