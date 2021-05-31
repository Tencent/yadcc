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
  name = 'messages_proto',
  srcs = 'messages.proto',
)

cc_library(
  name = 'local_task_monitor',
  hdrs = 'local_task_monitor.h',
  srcs = 'local_task_monitor.cc',
  deps = [
    '//flare/base:never_destroyed',
    '//flare/base:string',
    '//flare/base/internal:cpu',
    '//flare/fiber:fiber',
    '//thirdparty/gflags:gflags',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'local_task_monitor_test',
  srcs = 'local_task_monitor_test.cc',
  deps = [
    ':local_task_monitor',
    '//flare/fiber:fiber',
    '//flare/testing:main',
    '//thirdparty/gflags:gflags',
  ]
)

cc_library(
  name = 'distributed_cache_reader',
  hdrs = 'distributed_cache_reader.h',
  srcs = 'distributed_cache_reader.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:chrono',
    '//flare/base:compression',
    '//flare/base:hazptr',
    '//flare/base:never_destroyed',
    '//flare/base/experimental:bloom_filter',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//thirdparty/xxhash:xxhash',
    '//yadcc/api:cache_proto_flare',
    '//yadcc/api:env_desc_proto',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/common:xxhash',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'distributed_cache_reader_test',
  srcs = 'distributed_cache_reader_test.cc',
  deps = [
    ':distributed_cache_reader',
    '//flare/base:compression',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/rpc:rpc',
    '//flare/testing:main',
    '//flare/testing:rpc_mock',
    '//thirdparty/xxhash:xxhash',
    '//yadcc/api:cache_proto_flare',
  ]
)

cc_library(
  name = 'distributed_task_dispatcher',
  hdrs = 'distributed_task_dispatcher.h',
  srcs = 'distributed_task_dispatcher.cc',
  deps = [
    ':distributed_cache_reader',
    '//flare/base:buffer',
    '//flare/base:enum',
    '//flare/base:never_destroyed',
    '//flare/base:status',
    '//flare/base:string',
    '//flare/base/internal:time_view',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'distributed_task_dispatcher_test',
  srcs = 'distributed_task_dispatcher_test.cc',
  deps = [
    ':distributed_task_dispatcher',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//flare/testing:redis_mock',
    '//flare/testing:rpc_mock',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:scheduler_proto_flare',
  ]
)

cc_library(
  name = 'http_service_impl',
  hdrs = 'http_service_impl.h',
  srcs = 'http_service_impl.cc',
  deps = [
    ':distributed_task_dispatcher',
    ':local_task_monitor',
    ':messages_proto',
    '//flare/base:encoding',
    '//flare/base:exposed_var',
    '//flare/rpc:http',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)
