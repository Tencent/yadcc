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

#ifndef YADCC_CLIENT_TASK_QUOTA_H_
#define YADCC_CLIENT_TASK_QUOTA_H_

#include <chrono>
#include <cstddef>
#include <memory>

// We always acquire a task quota from our delegate daemon before starting a
// job.
//
// This prevents us from overloading the local machine.

namespace yadcc::client {

// Try acquire a task quota from the delegate daemon. If the quota can't be
// allocated in `timeout`, an empty handle is returned.
//
// The task quota is released automatically when the handle returned is
// destroyed.
std::shared_ptr<void> TryAcquireTaskQuota(bool lightweight_task,
                                          std::chrono::nanoseconds timeout);

// Wait until a task quota is available.
//
// The task quota is released automatically when the handle returned is
// destroyed.
std::shared_ptr<void> AcquireTaskQuota(bool lightweight_task);

}  // namespace yadcc::client

#endif  // YADCC_CLIENT_TASK_QUOTA_H_
