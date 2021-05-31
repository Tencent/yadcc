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

#ifndef YADCC_DAEMON_COMMON_FLAGS_H_
#define YADCC_DAEMON_COMMON_FLAGS_H_

#include "thirdparty/gflags/gflags.h"

// GFlags shared by multiple targets are declared here.

// Location of our scheduler.
DECLARE_string(scheduler_uri);

// Cache server for caching compilation result.
DECLARE_string(cache_server_uri);

// Token to access scheduler / cache server.
DECLARE_string(token);

// Directory for storing temporary files.
DECLARE_string(temporary_dir);

namespace yadcc::daemon {

// This version is bumped only if we think an upgrade should be performed by our
// script. (Usually a major version bump, if `major.minor.build` style is used.)
//
// For compatible changes (i.e., a minor version bump), this version is not
// changed.
extern int version_for_upgrade;

}  // namespace yadcc::daemon

#endif  // YADCC_DAEMON_COMMON_FLAGS_H_
