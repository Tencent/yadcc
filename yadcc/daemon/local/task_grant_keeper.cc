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

#include "yadcc/daemon/local/task_grant_keeper.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>

#include "flare/base/deferred.h"
#include "flare/base/logging.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/future.h"
#include "flare/fiber/this_fiber.h"
#include "flare/rpc/rpc_client_controller.h"

#include "yadcc/daemon/common_flags.h"

using namespace std::literals;

namespace yadcc::daemon::local {

TaskGrantKeeper::TaskGrantKeeper() : scheduler_stub_(FLAGS_scheduler_uri) {}

std::optional<TaskGrantKeeper::GrantDesc> TaskGrantKeeper::Get(
    const EnvironmentDesc& desc, const std::chrono::nanoseconds& timeout) {
  PerEnvGrantKeeper* keeper;
  {
    std::scoped_lock _(lock_);
    auto&& e = keepers_[desc.compiler_digest()];
    if (!e) {
      e = std::make_unique<PerEnvGrantKeeper>();
      e->env_desc = desc;
      e->fetcher =
          flare::Fiber([this, env = e.get()] { GrantFetcherProc(env); });
    }
    keeper = e.get();
  }

  std::unique_lock lk(keeper->lock);
  // Drop expired grants first.
  while (!keeper->remaining.empty() &&
         // We don't compensate for network delay here. We've already done that
         // in `GrantFetcherProc`.
         keeper->remaining.front().expires_at <
             flare::ReadCoarseSteadyClock()) {
    keeper->remaining.pop();
  }

  // We still have some. Satisfied without incur an RPC.
  if (!keeper->remaining.empty()) {
    auto result = keeper->remaining.front();
    keeper->remaining.pop();
    return result;
  }

  ++keeper->waiters;
  flare::ScopedDeferred _([&] { FLARE_CHECK_GE(--keeper->waiters, 0); });

  keeper->need_more_cv.notify_all();
  if (!keeper->available_cv.wait_for(
          lk, timeout, [&] { return !keeper->remaining.empty(); })) {
    return {};
  }
  auto result = keeper->remaining.front();
  keeper->remaining.pop();
  return result;
}

void TaskGrantKeeper::Free(std::uint64_t grant_id) {
  struct Context {
    scheduler::FreeTaskRequest req;
    flare::RpcClientController ctlr;
  };

  auto ctx = std::make_shared<Context>();
  ctx->req.set_token(FLAGS_token);
  ctx->req.add_task_grant_ids(grant_id);
  ctx->ctlr.SetTimeout(5s);

  // Done asynchronously, the result is discard. Failure doesn't harm.
  scheduler_stub_.FreeTask(ctx->req, &ctx->ctlr)
      .Then([ctx = ctx, grant_id](auto result) {
        FLARE_LOG_WARNING_IF(
            !result, "Failed to free task grant [{}]. Ignoring", grant_id);
      });
}

void TaskGrantKeeper::Stop() {
  std::scoped_lock _(lock_);
  leaving_.store(true, std::memory_order_relaxed);
  for (auto&& [_, v] : keepers_) {
    v->need_more_cv.notify_all();
  }
}

void TaskGrantKeeper::Join() {
  // Locking should not be necessary here as not one else could have been able
  // to add new elements to the map once `Stop()` finishes.
  for (auto&& [_, v] : keepers_) {
    v->fetcher.join();
  }
}

void TaskGrantKeeper::GrantFetcherProc(PerEnvGrantKeeper* keeper) {
  constexpr auto kMaxWait = 5s;
  // Tolerance of possible network delay.
  constexpr auto kNetworkDelayTolerance = 5s;
  constexpr auto kExpiresIn = 15s;

  static_assert(kExpiresIn > kMaxWait + kNetworkDelayTolerance + 1s,
                "Otherwise the grant can possibly expire immediately after RPC "
                "finishes..");

  while (!leaving_.load(std::memory_order_relaxed)) {
    std::unique_lock lk(keeper->lock);
    keeper->need_more_cv.wait(lk, [&] {
      return leaving_.load(std::memory_order_relaxed) ||
             keeper->remaining.empty();
    });
    if (leaving_.load(std::memory_order_relaxed)) {
      break;
    }

    auto before_rpc_now = flare::ReadCoarseSteadyClock();
    scheduler::WaitForStartingTaskRequest req;
    flare::RpcClientController ctlr;

    req.set_token(FLAGS_token);
    req.set_milliseconds_to_wait(kMaxWait / 1ms);
    req.set_next_keep_alive_in_ms(kExpiresIn / 1ms);
    *req.mutable_env_desc() = keeper->env_desc;
    req.set_immediate_reqs(keeper->waiters);
    req.set_prefetch_reqs(1);
    req.set_min_version(version_for_upgrade);
    ctlr.SetTimeout(kMaxWait + 5s);

    // We don't want to hold lock during RPC.
    lk.unlock();
    auto result = flare::fiber::BlockingGet(
        scheduler_stub_.WaitForStartingTask(req, &ctlr));
    lk.lock();
    if (result) {
      // Per method definition the scheduler is not required to wait until all
      // desired grants are available. Instead, the scheduler is permitted to
      // satisfy part of our requests. So don't assume the size of the result
      // array.
      for (int i = 0; i != result->grants().size(); ++i) {
        keeper->remaining.push(GrantDesc{
            // Using timestamp prior to RPC issue, let's be conservative.
            .expires_at = before_rpc_now + kExpiresIn - kNetworkDelayTolerance,
            .grant_id = result->grants(i).task_grant_id(),
            .servant_location = result->grants(i).servant_location()});
      }
      keeper->available_cv.notify_all();
    } else {
      if (result.error().code() != scheduler::STATUS_NO_QUOTA_AVAILABLE ||
          req.immediate_reqs()) {
        FLARE_LOG_WARNING("Failed to acquire grant for starting new task: {}",
                          result.error().ToString());
      } else {
        FLARE_VLOG(1,
                   "Unable to prefetch grant for possible new-coming task. The "
                   "cloud is busy.");
      }
      // Sleep for a while before retry if we fail.
      flare::this_fiber::SleepFor(100ms);
      // Retry then, hopefully now we fetched more grants to start new tasks.
    }
  }
}

}  // namespace yadcc::daemon::local
