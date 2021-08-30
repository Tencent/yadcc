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

// This file provides necessary utilities for packing / unpacking response /
// requests from our client.

#ifndef YADCC_DAEMON_LOCAL_PACKING_H_
#define YADCC_DAEMON_LOCAL_PACKING_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/util/json_util.h"

#include "flare/base/buffer.h"
#include "flare/base/demangle.h"
#include "flare/base/enum.h"
#include "flare/base/expected.h"
#include "flare/base/status.h"
#include "flare/base/string.h"
#include "flare/net/http/types.h"

#include "yadcc/daemon/local/multi_chunk.h"

namespace yadcc::daemon::local {

// Parse JSON string as `T`.
template <class T>
flare::Expected<T, flare::Status> TryParseJsonAsMessage(
    const std::string& str) {
  FLARE_VLOG(1, "Parsing: {}", str);
  T message;
  auto result = google::protobuf::util::JsonStringToMessage(str, &message);
  if (result.ok()) {
    FLARE_VLOG(1, "Parsed: {}", message.ShortDebugString());
    return message;
  }
  FLARE_VLOG(1, "Failed to parse: {}", result.ToString());
  return flare::Status{result.error_code(), result.error_message().ToString()};
}

// Parse JSON string as `T`, attachments may apply.
template <class T>
flare::Expected<std::pair<T, std::vector<flare::NoncontiguousBuffer>>,
                flare::Status>
TryParseMultiChunkRequest(const flare::NoncontiguousBuffer& bytes) {
  auto parts = TryParseMultiChunk(bytes);
  if (!parts || parts->empty()) {
    return flare::Status(flare::HttpStatus::BadRequest,
                         "Failed to parse the request as multi-chunk.");
  }
  auto req_msg = TryParseJsonAsMessage<T>(flare::FlattenSlow(parts->front()));
  if (!req_msg) {
    return flare::Status(flare::HttpStatus::BadRequest,
                         flare::Format("Failed to parse request: {}",
                                       req_msg.error().ToString()));
  }
  FLARE_VLOG(1, "Parsed request of type [{}] with {} attachments.",
             flare::GetTypeName<T>(), parts->size() - 1);
  return std::pair(*req_msg, std::vector<flare::NoncontiguousBuffer>(
                                 parts->begin() + 1, parts->end()));
}

// Write `message` as JSON.
std::string WriteMessageAsJson(const google::protobuf::Message& message);

// Write `message` as JSON, attachments may apply.
flare::NoncontiguousBuffer WriteMultiChunkResponse(
    const google::protobuf::Message& message,
    std::vector<flare::NoncontiguousBuffer> bytes);

}  // namespace yadcc::daemon::local

#endif  // YADCC_DAEMON_LOCAL_PACKING_H_
