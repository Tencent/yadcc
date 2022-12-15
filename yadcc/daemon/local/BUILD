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
  deps = [
    '//yadcc/api:patch_proto',
  ],
  visibility = '//yadcc/daemon/local/...'
)

cc_library(
  name = 'distributed_task',
  hdrs = 'distributed_task.h',
  deps = [
    '//flare/base:buffer',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:env_desc_proto',
  ],
  visibility = '//yadcc/daemon/local/distributed_task/...'
)

cc_library(
  name = 'file_digest_cache',
  hdrs = 'file_digest_cache.h',
  srcs = 'file_digest_cache.cc',
  deps = [
    '//flare/base:exposed_var',
    '//flare/base:logging',
    '//thirdparty/jsoncpp:jsoncpp',
  ],
  visibility = '//yadcc/daemon/local/...',
)

cc_test(
  name = 'file_digest_cache_test',
  srcs = 'file_digest_cache_test.cc',
  deps = [
    ':file_digest_cache',
  ]
)

cc_library(
  name = 'local_task_monitor',
  hdrs = 'local_task_monitor.h',
  srcs = 'local_task_monitor.cc',
  deps = [
    '//flare/base:exposed_var',
    '//flare/base:never_destroyed',
    '//flare/base:string',
    '//flare/base/internal:cpu',
    '//flare/fiber:fiber',
    '//thirdparty/gflags:gflags',
  ],
  visibility = [
    '//yadcc/daemon:yadcc-daemon',
  ]
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
    '//yadcc/common:xxhash',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = [
    '//yadcc/daemon:yadcc-daemon',
  ]
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
  name = 'config_keeper',
  hdrs = 'config_keeper.h',
  srcs = 'config_keeper.cc',
  deps = [
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:common_flags',
  ]
)

cc_test(
  name = 'config_keeper_test',
  srcs = 'config_keeper_test.cc',
  deps = [
    ':config_keeper',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//flare/testing:rpc_mock',
    '//yadcc/api:scheduler_proto_flare',
  ]
)

cc_library(
  name = 'task_grant_keeper',
  hdrs = 'task_grant_keeper.h',
  srcs = 'task_grant_keeper.cc',
  deps = [
    '//flare/base:deferred',
    '//flare/base:logging',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:common_flags',
  ]
)

cc_test(
  name = 'task_grant_keeper_test',
  srcs = 'task_grant_keeper_test.cc',
  deps = [
    ':task_grant_keeper',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//flare/testing:rpc_mock',
    '//yadcc/api:scheduler_proto_flare',
  ]
)

cc_library(
  name = 'distributed_task_dispatcher',
  hdrs = 'distributed_task_dispatcher.h',
  srcs = 'distributed_task_dispatcher.cc',
  deps = [
    ':config_keeper',
    ':distributed_cache_reader',
    ':distributed_task',
    ':running_task_keeper',
    ':task_grant_keeper',
    '//flare/base:buffer',
    '//flare/base:enum',
    '//flare/base:expected',
    '//flare/base:never_destroyed',
    '//flare/base:status',
    '//flare/base:string',
    '//flare/base/buffer:packing',
    '//flare/base/internal:time_view',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = [
    '//yadcc/daemon:yadcc-daemon',
  ]
)

cc_test(
  name = 'distributed_task_dispatcher_test',
  srcs = 'distributed_task_dispatcher_test.cc',
  deps = [
    ':distributed_task_dispatcher',
    '//flare/base/buffer:packing',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//flare/testing:redis_mock',
    '//flare/testing:rpc_mock',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:task_digest',
  ]
)

cc_library(
  name = 'packing',
  hdrs = 'packing.h',
  srcs = 'packing.cc',
  deps = [
    ':multi_chunk',
    '//flare/base:buffer',
    '//flare/base:demangle',
    '//flare/base:enum',
    '//flare/base:expected',
    '//flare/base:status',
    '//flare/base:string',
    '//flare/net/http:types',
    '//thirdparty/protobuf:protobuf',
  ]
)

cc_test(
  name = 'packing_test',
  srcs = 'packing_test.cc',
  deps = [
    ':packing',
    '//flare/testing:message_proto',
    '//thirdparty/jsoncpp:jsoncpp',
  ]
)

cc_library(
  name = 'http_service_impl',
  hdrs = 'http_service_impl.h',
  srcs = 'http_service_impl.cc',
  deps = [
    ':distributed_task_dispatcher',
    ':file_digest_cache',
    ':local_task_monitor',
    ':messages_proto',
    ':packing',
    '//flare/base:encoding',
    '//flare/rpc:http',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
    '//yadcc/daemon/local/distributed_task:cxx_compilation_task',
  ],
  visibility = [
    '//yadcc/daemon:yadcc-daemon',
  ]
)

cc_test(
  name = 'http_service_impl_test',
  srcs = 'http_service_impl_test.cc',
  deps = [
    ':file_digest_cache',
    ':http_service_impl',
    ':multi_chunk',
    '//flare/init:override_flag',
    '//flare/testing:hooking_mock',
    '//flare/testing:main',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/api:extra_info_proto',
  ]
)

cc_library(
  name = 'running_task_keeper',
  hdrs = 'running_task_keeper.h',
  srcs = 'running_task_keeper.cc',
  deps = [
    '//flare/base:chrono',
    '//flare/base:string',
    '//flare/fiber:timer',
    '//flare/rpc:rpc',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:common_flags',
    '//yadcc/daemon:task_digest',
  ]
)

cc_test(
  name = 'running_task_keeper_test',
  srcs = 'running_task_keeper_test.cc',
  deps = [
    ':running_task_keeper',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//flare/testing:rpc_mock',
    '//thirdparty/googletest:gmock',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/daemon:task_digest',
  ]
)

cc_library(
  name = 'multi_chunk',
  hdrs = 'multi_chunk.h',
  srcs = 'multi_chunk.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:string',
  ]
)

cc_test(
  name = 'multi_chunk_test',
  srcs = 'multi_chunk_test.cc',
  deps = [
    ':multi_chunk',
  ]
)
