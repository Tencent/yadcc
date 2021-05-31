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

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>

#include <chrono>
#include <memory>

#include "thirdparty/gflags/gflags.h"

#include "flare/base/string.h"
#include "flare/fiber/this_fiber.h"
#include "flare/init.h"
#include "flare/init/override_flag.h"
#include "flare/rpc/server.h"
#include "flare/rpc/server_group.h"

#include "yadcc/common/inspect_auth.h"
#include "yadcc/daemon/cloud/compiler_registry.h"
#include "yadcc/daemon/cloud/daemon_service_impl.h"
#include "yadcc/daemon/cloud/distributed_cache_writer.h"
#include "yadcc/daemon/cloud/execution_engine.h"
#include "yadcc/daemon/cloud/sysinfo.h"
#include "yadcc/daemon/common_flags.h"
#include "yadcc/daemon/local/distributed_cache_reader.h"
#include "yadcc/daemon/local/distributed_task_dispatcher.h"
#include "yadcc/daemon/local/http_service_impl.h"
#include "yadcc/daemon/local/local_task_monitor.h"
#include "yadcc/daemon/privilege.h"

using namespace std::literals;

// Technically we're implementing two non-related roles:
//
// - Serving requests from local client (compiler wrapper) and submit
//   compilation tasks to the cloud.
//
// - Accepting compilation tasks from the cloud and execute it.
//
// If we implement them differently (in two program), things would be much more
// clear.
//
// There's a reason why we don't do that. As we currently imagine, most of our
// user should be both task submitter and, when their machines are idle, acting
// as a compile-server resource provider. If we separate these two roles into
// two program, we'd incur an unnecessary maintainance overhead.

DEFINE_int32(local_port, 8334 /* Got it from `random.random()` */,
             "This port serves requests from our local client, and may only be "
             "connected through loopback interface.");
DEFINE_string(
    serving_ip, "",
    "If set, this should be an IP address to which this daemon can be reached. "
    "Leaving it empty to let the program determine this automatically.");
DEFINE_int32(serving_port, 8335,
             "This port serves requests from other daemon on the network. It "
             "can be connected via any interface attached to the host.");
DEFINE_bool(allow_core_dump, false,
            "Unless set, we disable core-dump for daemon by default.");

FLARE_OVERRIDE_FLAG(logbufsecs, 0);

FLARE_OVERRIDE_FLAG(flare_rpc_server_max_packet_size, 67108864);
FLARE_OVERRIDE_FLAG(flare_rpc_channel_max_packet_size, 67108864);

// We don't want too many connections. Linux force a TCP slow-start after a
// connection has been idle for a while (hundreds of milliseconds, depending on
// RTT). That really hurts our workload (burst large chunk transfer.). By
// keeping connection numbers low, we alleviate to some degree.
//
// OTOH, using only 1 connection can really hurt throughput for connection
// between DCs in different district.. (I measured a bandwidth of < 20Mbps using
// `iperf` on busy hours. Using multi connections does help here.).
//
// Hopefully Flare will support QUIC to help us to get ride of this restriction.
FLARE_OVERRIDE_FLAG(flare_rpc_client_max_connections_per_server, 2);
FLARE_OVERRIDE_FLAG(flare_concurrency_hint, 4);  // Large enough.

namespace yadcc::daemon {

namespace {

void DisableCoreDump() {
  rlimit limit = {.rlim_cur = 0, .rlim_max = 0};
  FLARE_LOG_ERROR_IF(setrlimit(RLIMIT_CORE, &limit) != 0,
                     "Failed to disable coredump.");
}

std::string GetPrivateNetworkAddress() {
  static std::string result = []() -> std::string {
    if (!FLAGS_serving_ip.empty()) {
      return FLAGS_serving_ip;
    }

    // Determine on which IP we should be serving.
    auto endpoints = flare::GetInterfaceAddresses();
    for (auto&& e : endpoints) {
      if (flare::IsPrivateIpv4AddressCorp(e)) {
        return flare::EndpointGetIp(e);
      }
    }
    FLARE_LOG_FATAL(
        "Failed to determine private IP address of this node. You need to "
        "specify `serving_ip` yourself.");
  }();
  return result;
}

// Remove `{temp_dir}/yadcc_*`.
//
// This method usually is effectively a no-op. But in case we crashed last run,
// this method clears up our workspace.
void RemoveTemporaryFilesCreateDuringOurPastLife() {
  std::unique_ptr<DIR, void (*)(DIR*)> dir{opendir(FLAGS_temporary_dir.c_str()),
                                           [](auto ptr) { closedir(ptr); }};
  FLARE_CHECK(dir, "Failed to open `{}`.", FLAGS_temporary_dir);

  while (auto ep = readdir(dir.get())) {
    if (flare::StartsWith(ep->d_name, "yadcc_")) {
      auto fullname = flare::Format("{}/{}", FLAGS_temporary_dir, ep->d_name);
      if (unlink(fullname.c_str()) != 0) {
        FLARE_LOG_WARNING("Failed to remove [{}]: [{}] {}", fullname, errno,
                          strerror(errno));
      } else {
        FLARE_LOG_INFO("Removed [{}]", fullname);
      }
    }
  }
}

}  // namespace

int DaemonStart(int argc, char** argv) {
  // Reset environment variables that can affect how GCC behaves.
  //
  // TODO(luobogao): We can instead pass environment variables from client to
  // GCC. This can reduce cache hit ratio though.
  //
  // @sa: https://gcc.gnu.org/onlinedocs/gcc/Environment-Variables.html
  setenv("LC_ALL", "en_US.utf8", true);  // Hardcoded to UTF-8.
  unsetenv("GCC_COMPARE_DEBUG");
  unsetenv("SOURCE_DATE_EPOCH");

  // Drop privileges if we're running as privileged.
  DropPrivileges();

  // Usually we don't want to generate core dump on user's machine.
  if (!FLAGS_allow_core_dump) {
    DisableCoreDump();
  }

  // Remove everything matches `{temp_dir}/yadcc_*`. If there are any, those
  // files are there because we didn't exit cleanly last time.
  RemoveTemporaryFilesCreateDuringOurPastLife();

  // Initialize the singletons early..
  cloud::InitializeSystemInfo();
  (void)cloud::CompilerRegistry::Instance();
  (void)cloud::DistributedCacheWriter::Instance();
  (void)local::DistributedCacheReader::Instance();
  (void)local::DistributedTaskDispatcher::Instance();
  (void)local::LocalTaskMonitor::Instance();

  // TODO(luobogao): Set up a timer which periodically if we're still on disk.
  // If not we'd better leave (to prevent some weird output from compilation.).
  //
  // This is partly mitigated in `ExecuteCommand` by resetting CWD to `/` before
  // running compiler.

  FLARE_LOG_INFO("Using scheduler at [{}].", FLAGS_scheduler_uri);
  FLARE_LOG_INFO("Using cache server at [{}].", FLAGS_cache_server_uri);

  flare::ServerGroup server_group;

  // Initialize daemon serving requests from local client.
  auto local_daemon = std::make_unique<flare::Server>();
  local_daemon->AddProtocol("http");
  local_daemon->AddHttpHandler(std::regex(R"(\/local\/.*)"),
                               std::make_unique<local::HttpServiceImpl>());
  local_daemon->ListenOn(  // Or perhaps we can use a UNIX socket?
      flare::EndpointFromIpv4("127.0.0.1", FLAGS_local_port));
  // This daemon listens on localhost only, therefore it's safe not to apply a
  // basic-auth filter on `/inspect/`.

  // Initialize daemon serving requests from network.
  auto serving_daemon = std::make_unique<flare::Server>();
  cloud::DaemonServiceImpl daemon_svc(
      flare::Format("{}:{}", GetPrivateNetworkAddress(), FLAGS_serving_port));
  // FIXME: What about IPv6?
  serving_daemon->AddProtocol("flare");
  serving_daemon->AddService(&daemon_svc);
  serving_daemon->AddHttpFilter(MakeInspectAuthFilter());
  serving_daemon->ListenOn(
      flare::EndpointFromIpv4("0.0.0.0", FLAGS_serving_port));

  // Start our server.
  server_group.AddServer(std::move(local_daemon));
  server_group.AddServer(std::move(serving_daemon));
  server_group.Start();

  // Wait until asked to quit.
  flare::WaitForQuitSignal();

  // Stop accessing new requests.
  server_group.Stop();

  // Flush running tasks.
  cloud::ExecutionEngine::Instance()->Stop();
  cloud::DistributedCacheWriter::Instance()->Stop();
  local::DistributedTaskDispatcher::Instance()->Stop();
  local::DistributedCacheReader::Instance()->Stop();
  daemon_svc.Stop();

  cloud::ExecutionEngine::Instance()->Join();
  cloud::DistributedCacheWriter::Instance()->Join();
  local::DistributedTaskDispatcher::Instance()->Join();
  local::DistributedCacheReader::Instance()->Join();
  cloud::ShutdownSystemInfo();
  daemon_svc.Join();

  server_group.Join();

  quick_exit(0);  // BUG: For the moment we don't exit cleanly.
  return 0;
}

}  // namespace yadcc::daemon

int main(int argc, char** argv) {
  return flare::Start(argc, argv, yadcc::daemon::DaemonStart);
}
