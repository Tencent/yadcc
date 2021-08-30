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

#include "yadcc/daemon/local/packing.h"

namespace yadcc::daemon::local {

std::string WriteMessageAsJson(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions opts;
  opts.always_print_primitive_fields = true;
  opts.always_print_enums_as_ints = true;
  opts.preserve_proto_field_names = true;

  std::string result;
  auto status =
      google::protobuf::util::MessageToJsonString(message, &result, opts);
  FLARE_CHECK(status.ok());  // How can it fail?
  return result;
}

flare::NoncontiguousBuffer WriteMultiChunkResponse(
    const google::protobuf::Message& message,
    std::vector<flare::NoncontiguousBuffer> bytes) {
  bytes.insert(bytes.begin(),
               flare::CreateBufferSlow(WriteMessageAsJson(message)));
  return MakeMultiChunk(std::move(bytes));
}

}  // namespace yadcc::daemon::local
