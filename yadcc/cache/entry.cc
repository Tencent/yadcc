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

#include "yadcc/cache/cache_service_impl.h"

#include "flare/init.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/server.h"

#include "yadcc/common/inspect_auth.h"

DEFINE_int32(port, 8337, "Port we'll be listening on.");

FLARE_OVERRIDE_FLAG(logbufsecs, 0);

// We're disk I/O intensive, and will likely be blocked (waiting on disk)
// frequently, so start some spare threads.
FLARE_OVERRIDE_FLAG(flare_concurrency_hint, 32);

// As obvious.
FLARE_OVERRIDE_FLAG(flare_rpc_server_max_packet_size, 67108864);
FLARE_OVERRIDE_FLAG(flare_rpc_channel_max_packet_size, 67108864);

namespace yadcc::cache {

int Entry(int argc, char** argv) {
  // Initialize our service first.
  CacheServiceImpl service_impl;
  service_impl.Start();

  // And start a server to serve requests.
  flare::Server server;
  server.AddProtocol("flare");
  server.AddService(&service_impl);
  server.AddHttpFilter(MakeInspectAuthFilter());
  server.ListenOn(flare::EndpointFromIpv4("0.0.0.0", FLAGS_port));  // IPv6?
  server.Start();

  // Wait until asked to quit.
  flare::WaitForQuitSignal();
  server.Stop();
  server.Join();
  service_impl.Stop();
  service_impl.Join();

  return 0;
}

}  // namespace yadcc::cache

int main(int argc, char** argv) {
  return flare::Start(argc, argv, yadcc::cache::Entry);
}
