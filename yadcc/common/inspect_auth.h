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

#ifndef YADCC_COMMON_INSPECT_AUTH_H_
#define YADCC_COMMON_INSPECT_AUTH_H_

#include <memory>

#include "flare/rpc/http_filter.h"

namespace yadcc {

// Returns a HTTP filter which performs authentication for requests to
// `/inspect/...`.
//
// This is necessary for our token-based authentication to be effective. Since
// token is specified via GFlags, if `/inspect/gflags` is freely available,
// anyone can read acceptable tokens via this interface.
std::unique_ptr<flare::HttpFilter> MakeInspectAuthFilter();

}  // namespace yadcc

#endif  // YADCC_COMMON_INSPECT_AUTH_H_
