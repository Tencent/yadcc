# Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
#
# Licensed under the BSD 3-Clause License (the "License"); you may not use this
# file except in compliance with the License. You may obtain a copy of the
# License at
#
# https://opensource.org/licenses/BSD-3-Clause
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

proto_library(
  name = 'cache_meta_proto',
  srcs = 'cache_meta.proto',
)

cc_library(
  name = 'cache_format',
  hdrs = 'cache_format.h',
  srcs = 'cache_format.cc',
  deps = [
    ':cache_meta_proto',
    '//flare/base:buffer',
    '//flare/base:encoding',
    '//flare/base:endian',
    '//flare/base:string',
    '//flare/base/buffer:zero_copy_stream',
    '//flare/base/crypto:blake3',
    '//yadcc/api:env_desc_proto',
  ],
  visibility = '//yadcc/daemon/...',
)

cc_test(
  name = 'cache_format_test',
  srcs = 'cache_format_test.cc',
  deps = [
    ':cache_format',
    '//yadcc/api:env_desc_proto',
  ]
)

cc_library(
  name = 'common_flags',
  hdrs = 'common_flags.h',
  srcs = 'common_flags.cc',
  deps = [
    '//thirdparty/gflags:gflags',
  ],
  visibility = '//yadcc/daemon/...',
)

cc_library(
  name = 'privilege',
  hdrs = 'privilege.h',
  srcs = 'privilege.cc',
  deps = [
    '//flare/base:logging',
  ]
)

cc_binary(
  name = 'yadcc-daemon',
  srcs = 'entry.cc',
  deps = [
    ':common_flags',
    ':privilege',
    '//flare:init',
    '//flare/base:string',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/rpc:rpc',
    '//flare/rpc/builtin:tcmalloc_profiler_http_handler',
    '//thirdparty/gflags:gflags',
    '//thirdparty/gperftools:tcmalloc_and_profiler',
    '//yadcc/common:inspect_auth',
    '//yadcc/daemon/cloud:compiler_registry',
    '//yadcc/daemon/cloud:daemon_service_impl',
    '//yadcc/daemon/cloud:distributed_cache_writer',
    '//yadcc/daemon/cloud:execution_engine',
    '//yadcc/daemon/cloud:sysinfo',
    '//yadcc/daemon/local:distributed_cache_reader',
    '//yadcc/daemon/local:distributed_task_dispatcher',
    '//yadcc/daemon/local:http_service_impl',
    '//yadcc/daemon/local:local_task_monitor',
  ]
)
