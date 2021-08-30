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

#include "gflags/gflags.h"

#include "flare/init.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/server.h"

#include "yadcc/common/inspect_auth.h"
#include "yadcc/scheduler/scheduler_service_impl.h"
#include "yadcc/scheduler/task_dispatcher.h"

DEFINE_int32(port, 8336, "Port the scheduler will be listening on.");

FLARE_OVERRIDE_FLAG(logbufsecs, 0);
FLARE_OVERRIDE_FLAG(flare_concurrency_hint, 8);  // Large enough.

namespace yadcc::scheduler {

int SchedulerStart(int argc, char** argv) {
  // Initialize the singleton.
  TaskDispatcher::Instance();

  flare::Server server;

  // Start server.
  server.AddProtocol("flare");
  server.AddHttpFilter(MakeInspectAuthFilter());
  server.AddService(std::make_unique<SchedulerServiceImpl>());
  // TODO(luobogao): What about IPv6?
  server.ListenOn(
      // We can't listen on loopback only, as obvious.
      flare::EndpointFromIpv4("0.0.0.0", FLAGS_port));
  server.Start();

  // Wait until asked to quit.
  flare::WaitForQuitSignal();
  server.Stop();
  server.Join();

  return 0;
}

}  // namespace yadcc::scheduler

int main(int argc, char** argv) {
  return flare::Start(argc, argv, yadcc::scheduler::SchedulerStart);
}
