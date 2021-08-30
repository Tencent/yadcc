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

#ifndef YADCC_CLIENT_CXX_LIBFAKEROOT_H_
#define YADCC_CLIENT_CXX_LIBFAKEROOT_H_

#include <string>

namespace yadcc::client {

// `libfakeroot.so` helps us to rewrite compiler's absolute path in the
// resulting preprocessed file.
//
// This is important for distributed caching of the compilation result to work.
//
// GCC embeds header paths into preprocessed file. These paths are primarily
// used for generating debug info (and diagnostics, if any), and should not
// affect code generation. This can be checked by comparing compilation result
// of preprocessed source code by hand with `-gz=none` (otherwise the debugging
// symbols may possibly be compressed, and hard for human to read).
//
// @sa: https://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html
//
// However, these paths do affect digest of preprocessed source file, leading to
// cache miss.
//
// Therefore, we use `libfakeroot.so` to rewrite those paths to a hardcoded one,
// so that cache can hit whenever possible, regardless of where the compiler is
// located. (Handling of working directory in preprocessed file is done
// separately by specifying `-fno-working-directory`.)
std::string GetLibFakeRootPath();

}  // namespace yadcc::client

#endif  // YADCC_CLIENT_CXX_LIBFAKEROOT_H_
