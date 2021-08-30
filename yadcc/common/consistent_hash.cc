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

#include "yadcc/common/consistent_hash.h"

namespace yadcc {

std::string ConsistentHash::GetNode(uint32_t hash) const {
  FLARE_CHECK(!hash_ring_.empty());
  auto dst_node = std::lower_bound(
      hash_ring_.begin(), hash_ring_.end(), hash,
      [](const std::pair<uint32_t, std::shared_ptr<std::string>>& node,
         const uint32_t value) { return node.first < value; });
  if (dst_node == hash_ring_.end()) {
    dst_node = hash_ring_.begin();
  }
  return *dst_node->second;
}

}  // namespace yadcc
