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

cc_library(
  name = 'temporary_file',
  hdrs = 'temporary_file.h',
  srcs = 'temporary_file.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:logging',
    '//yadcc/common:io',
  ]
)

cc_test(
  name = 'temporary_file_test',
  srcs = 'temporary_file_test.cc',
  deps = [
    ':temporary_file',
    '//flare/base:buffer',
    '//flare/base:string',
  ]
)

cc_library(
  name = 'compiler_registry',
  hdrs = 'compiler_registry.h',
  srcs = 'compiler_registry.cc',
  deps = [
    '//flare/base:encoding',
    '//flare/base:logging',
    '//flare/base:never_destroyed',
    '//flare/base:string',
    '//flare/base/crypto:blake3',
    '//thirdparty/gflags:gflags',
    '//yadcc/api:env_desc_proto',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'compiler_registry_test',
  srcs = 'compiler_registry_test.cc',
  deps = [
    ':compiler_registry',
    '//flare/base:logging',
    '//flare/base:string',
    '//thirdparty/gflags:gflags',
  ],
  testdata = [
    ('testdata', 'test-bin'),
  ]
)

cc_library(
  name = 'distributed_cache_writer',
  hdrs = 'distributed_cache_writer.h',
  srcs = 'distributed_cache_writer.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:future',
    '//flare/base:never_destroyed',
    '//flare/rpc:rpc',
    '//yadcc/api:cache_proto_flare',
    '//yadcc/api:env_desc_proto',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'distributed_cache_writer_test',
  srcs = 'distributed_cache_writer_test.cc',
  deps = [
    ':distributed_cache_writer',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//flare/testing:rpc_mock',
    '//yadcc/api:cache_proto_flare',
  ]
)

cc_library(
  name = 'execute_command',
  hdrs = 'execute_command.h',
  srcs = 'execute_command.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:logging',
    '//yadcc/common:io',
  ]
)

cc_test(
  name = 'execute_command_test',
  srcs = 'execute_command_test.cc',
  deps = [
    ':execute_command',
    '//yadcc/common:io',
  ]
)

cc_library(
  name = 'execution_engine',
  hdrs = 'execution_engine.h',
  srcs = 'execution_engine.cc',
  deps = [
    ':execute_command',
    ':temporary_file',
    '//flare/base:buffer',
    '//flare/base:expected',
    '//flare/base:exposed_var',
    '//flare/base:future',
    '//flare/base:logging',
    '//flare/base:string',
    '//flare/base/internal:cpu',
    '//flare/fiber:fiber',
    '//thirdparty/gflags:gflags',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/common:io',
    '//yadcc/common:parse_size',
    '//yadcc/daemon/cloud:sysinfo',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'execution_engine_test',
  srcs = 'execution_engine_test.cc',
  deps = [
    ':execution_engine',
    '//flare/base:buffer',
    '//flare/init:override_flag',
    '//flare/testing:main',
    '//thirdparty/gflags:gflags',
    '//thirdparty/googletest:gmock',
    '//thirdparty/googletest:gtest',
  ]
)

cc_library(
  name = 'daemon_service_impl',
  hdrs = 'daemon_service_impl.h',
  srcs = 'daemon_service_impl.cc',
  deps = [
    ':compiler_registry',
    ':distributed_cache_writer',
    ':execution_engine',
    ':sysinfo',
    '//flare/base:compression',
    '//flare/base:encoding',
    '//flare/base:enum',
    '//flare/base:string',
    '//flare/base/crypto:blake3',
    '//flare/base/internal:cpu',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//thirdparty/zstd:zstd',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/common:token_verifier',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:common_flags',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_library(
  name = 'sysinfo',
  hdrs = 'sysinfo.h',
  srcs = 'sysinfo.cc',
  deps = [
    '//flare/base:chrono',
    '//flare/base:logging',
    '//flare/base:string',
    '//flare/base/internal:cpu',
    '//flare/fiber:fiber',
  ],
  visibility = '//yadcc/daemon:yadcc-daemon',
)

cc_test(
  name = 'sysinfo_test',
  srcs = 'sysinfo_test.cc',
  deps = [
    '//flare/base:chrono',
    '//flare/base:logging',
    '//flare/base/internal:cpu',
    '//flare/fiber:fiber',
    '//flare/testing:main',
    '//yadcc/daemon/cloud:sysinfo',
  ]
)
