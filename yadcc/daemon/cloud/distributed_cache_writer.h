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

#ifndef YADCC_DAEMON_CLOUD_DISTRIBUTED_CACHE_WRITER_H_
#define YADCC_DAEMON_CLOUD_DISTRIBUTED_CACHE_WRITER_H_

#include <memory>
#include <string>

#include "flare/base/buffer.h"
#include "flare/base/future.h"

#include "yadcc/api/cache.flare.pb.h"

namespace yadcc::daemon::cloud {

// This class is responsible for updating our distributed compilation cache.
//
// Writing and reading cache are separated to two different classes. The reader
// side is in `yadcc/daemon/local`.
//
// I'm not sure if we should merge them into a `DistributedCache` or keep them
// in the current form: Separating them into two classes just looks weird. But
// OTOH, allowing anyone to write to our distributed cache can pose a security
// risk (Frankly though, so long as the user can act as a compile-server, it's
// inherently insecure.).
class DistributedCacheWriter {
 public:
  static DistributedCacheWriter* Instance();

  DistributedCacheWriter();
  ~DistributedCacheWriter();

  // Write a compilation result into the cache.
  flare::Future<bool> AsyncWrite(const std::string& key, int exit_code,
                                 const std::string& standard_output,
                                 const std::string& standard_error,
                                 const flare::NoncontiguousBuffer& buffer);

  void Stop();
  void Join();

 private:
  std::unique_ptr<cache::CacheService_AsyncStub> cache_stub_;
};

}  // namespace yadcc::daemon::cloud

#endif  // YADCC_DAEMON_CLOUD_DISTRIBUTED_CACHE_WRITER_H_
