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

#ifndef YADCC_CLIENT_SPAN_H_
#define YADCC_CLIENT_SPAN_H_

#include <cinttypes>

namespace yadcc::client {

// Dirty and quick implementation of `gsl::span`.
template <class T>
class Span {
 public:
  Span() = default;
  Span(T* begin, std::size_t size) : begin_(begin), end_(begin + size) {}
  Span(T* begin, T* end) : begin_(begin), end_(end) {}

  T* begin() const noexcept { return begin_; }
  T* end() const noexcept { return end_; }
  bool empty() const noexcept { return begin_ == end_; }
  std::size_t size() const noexcept { return end_ - begin_; }
  T& operator[](std::size_t index) const { return begin_[index]; }
  T& front() const noexcept { return *begin_; }
  T& back() const noexcept { return *(end_ - 1); }

 private:
  T* begin_ = nullptr;
  T* end_ = nullptr;
};

}  // namespace yadcc::client

#endif  //  YADCC_CLIENT_SPAN_H_
