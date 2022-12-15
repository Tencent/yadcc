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

#ifndef YADCC_DAEMON_SYSINFO_H_
#define YADCC_DAEMON_SYSINFO_H_

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace yadcc::daemon {

// Init the system info environment.
void InitializeSystemInfo();

// Clear the system info environment.
void ShutdownSystemInfo();

// A finer-grained method to get the loadavg of recent duration. Nullopt will
// return, if you pass more than 1min duration.
//
// The result is rounded up.
std::optional<std::size_t> TryGetProcessorLoad(std::chrono::seconds duration);

// Get 1min loadavg with calling getloadavg.
std::size_t GetProcessorLoadInLastMinute();

// To get number of processors in our host.
std::size_t GetNumberOfProcessors();

// To get available memory in our host.
std::size_t GetMemoryAvailable();

// To get total memory in our host.
std::size_t GetTotalMemory();

// To get available space size of directory.
std::size_t GetDiskAvailableSize(const std::string& dir);

}  // namespace yadcc::daemon

#endif  //  YADCC_DAEMON_SYSINFO_H_
