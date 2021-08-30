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

#ifndef YADCC_COMMON_CONSISTENT_HASH_H_
#define YADCC_COMMON_CONSISTENT_HASH_H_

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flare/base/logging.h"
#include "flare/base/string.h"

namespace yadcc {

// Consistent hash utility.
class ConsistentHash {
 public:
  template <typename T>
  ConsistentHash(const std::map<std::string, std::uint64_t>& weighted_dirs,
                 T&& hash_func);

  std::string GetNode(uint32_t hash) const;

 private:
  // To make the hash more uniform, this constant helps us get more virtual
  // nodes.
  static constexpr unsigned kVirtualNodeFactor = 100;

 private:
  std::vector<std::pair<uint32_t, std::shared_ptr<std::string>>> hash_ring_;
};

template <typename T>
ConsistentHash::ConsistentHash(
    const std::map<std::string, std::uint64_t>& weighted_dirs, T&& hash_func) {
  for (auto& [dir, weight] : weighted_dirs) {
    auto dir_ptr = std::make_shared<std::string>(dir);
    auto virtual_node_size = weight * kVirtualNodeFactor;
    for (unsigned i = 0; i < virtual_node_size; ++i) {
      auto virtual_node = flare::Format("{}#VN{}", dir, i);
      uint32_t hash_key = hash_func(virtual_node);
      hash_ring_.push_back(std::pair(hash_key, dir_ptr));
    }
  }
  std::sort(hash_ring_.begin(), hash_ring_.end(),
            [](const std::pair<uint32_t, std::shared_ptr<std::string>>& lhs,
               const std::pair<uint32_t, std::shared_ptr<std::string>>& rhs) {
              return lhs.first < rhs.first;
            });
}

}  // namespace yadcc

#endif  // YADCC_COMMON_CONSISTENT_HASH_H_
