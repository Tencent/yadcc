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

#ifndef YADCC_CLIENT_COMMON_UTILITY_H_
#define YADCC_CLIENT_COMMON_UTILITY_H_

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yadcc::client {

// Get base name of a given path.
std::string GetBaseName(const std::string& name);

// Get directory part of the given path.
std::string GetPathName(const std::string& name);

// Get canonicalized absolute pathname.
std::string GetCanonicalPath(const std::string& path);

// Get location of ourselves.
const std::string& GetSelfExecutable();

// Tests if `s` starts with `pattern`.
bool StartsWith(const std::string_view& s, const std::string_view& pattern);

// Tests if `s` ends with `pattern`.
bool EndsWith(const std::string_view& s, const std::string_view& pattern);

// Split string by `pattern`.
std::vector<std::string_view> Split(const std::string_view& text,
                                    const std::string_view& pattern);

// Concatenate strings, deliminated by `delim`.
std::string Join(const std::vector<std::string>& parts,
                 const std::string_view& delim);

// Find executable in `PATH`. This method won't return ourselves as the result,
// even if we're in path with exactly the same name as `executable`.
//
// `pred` is called for each match. The first match on which `pred` is satisfied
// is returned.
std::string FindExecutableInPath(const std::string& executable);
std::string FindExecutableInPath(
    const std::string& executable,
    const std::function<bool(const std::string& canonical_path)>& pred);

// Get coarse steady clock.
std::chrono::steady_clock::time_point ReadCoarseSteadyClock();

// Get mtime and size of a file.
std::pair<std::uint64_t, std::uint64_t> GetMtimeAndSize(
    const std::string& file);

// Blake3 digest of file.
std::string Blake3Digest(const std::string& file);

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_COMMON_UTILITY_H_
