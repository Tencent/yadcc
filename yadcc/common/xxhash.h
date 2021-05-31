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

#ifndef YADCC_COMMON_XXHASH_H_
#define YADCC_COMMON_XXHASH_H_

#include <string_view>

namespace yadcc {

// Adapts xxHash's C methods to C++ functor.
struct XxHash {
  std::size_t operator()(const std::string_view& str) const noexcept;
};

}  // namespace yadcc

#endif  // YADCC_COMMON_XXHASH_H_
