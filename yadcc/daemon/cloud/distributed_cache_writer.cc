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

#include "yadcc/daemon/cloud/distributed_cache_writer.h"

#include <chrono>

#include "flare/base/never_destroyed.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/daemon/cache_format.h"
#include "yadcc/daemon/common_flags.h"

using namespace std::literals;

namespace yadcc::daemon::cloud {

DistributedCacheWriter* DistributedCacheWriter::Instance() {
  static flare::NeverDestroyed<DistributedCacheWriter> writer;
  return writer.Get();
}

DistributedCacheWriter::DistributedCacheWriter() {
  if (!FLAGS_cache_server_uri.empty()) {
    cache_stub_ =
        std::make_unique<cache::CacheService_AsyncStub>(FLAGS_cache_server_uri);
  }
}

DistributedCacheWriter::~DistributedCacheWriter() {
  // Nothing yet.
}

flare::Future<bool> DistributedCacheWriter::AsyncWrite(
    const std::string& key, int exit_code, const std::string& standard_output,
    const std::string& standard_error,
    const flare::NoncontiguousBuffer& buffer) {
  if (!cache_stub_) {
    // Caching is not enabled at all.
    return true;
  }

  if (exit_code != 0) {
    // For the moment we don't cache failed compilation result.
    //
    // Note that, if our system is robust enough (this may require us to only
    // use server maintained by ourselves), we actually can cache failure
    // result. We don't want to recompile known-to-fail source code again and
    // again, right?
    return true;
  }

  struct Context {
    cache::PutEntryRequest req;
    flare::RpcClientController ctlr;
  };

  auto ctx = std::make_shared<Context>();

  ctx->req.set_token(FLAGS_token);
  ctx->req.set_key(key);
  ctx->ctlr.SetTimeout(5s);
  ctx->ctlr.SetRequestAttachment(WriteCacheEntry(
      CacheEntry{exit_code, standard_output, standard_error, buffer}));
  return cache_stub_->PutEntry(ctx->req, &ctx->ctlr)
      .Then([ctx, key](auto result) {
        if (!result) {
          FLARE_LOG_WARNING(
              "Failed to populate compilation cache entry [{}]: {}", key,
              result.error().ToString());
          return false;
        }
        return true;
      });
}

void DistributedCacheWriter::Stop() {
  // NOTHING.
}

void DistributedCacheWriter::Join() {
  // NOTHING.
}

}  // namespace yadcc::daemon::cloud
