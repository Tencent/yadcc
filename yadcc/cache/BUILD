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
  name = 'dir',
  hdrs = 'dir.h',
  srcs = 'dir.cc',
  deps = [
    '//flare/base:logging',
    '//flare/base:string',
  ]
)

cc_test(
  name = 'dir_test',
  srcs = 'dir_test.cc',
  deps = [
    ':dir',
    '//flare/base:logging',
  ]
)

cc_test(
  name = 'bloom_filter_generator_test',
  srcs = 'bloom_filter_generator_test.cc',
  deps = [
    ':bloom_filter_generator',
  ]
)

cc_library(
  name = 'bloom_filter_generator',
  hdrs = 'bloom_filter_generator.h',
  srcs = 'bloom_filter_generator.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:chrono',
    '//flare/base/experimental:bloom_filter',
    '//yadcc/common:xxhash',
  ]
)

cc_library(
  name = 'cache_service_impl',
  hdrs = 'cache_service_impl.h',
  srcs = 'cache_service_impl.cc',
  deps = [
    ':bloom_filter_generator',
    ':cache_engine',
    ':in_memory_cache',
    '//flare/base:compression',
    '//flare/base:random',
    '//flare/base:string',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//yadcc/api:cache_proto_flare',
    '//yadcc/common:parse_size',
    '//yadcc/common:token_verifier',
  ]
)

cc_test(
  name = 'cache_service_impl_test',
  srcs = 'cache_service_impl_test.cc',
  deps = [
    ':cache_engine',
    ':cache_service_impl',
    ':disk_cache_engine',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/rpc:rpc',
    '//flare/testing:main',
    '//flare/testing:rpc_controller',
    '//yadcc/api:cache_proto_flare',
  ]
)

cc_library(
  name = 'consistent_hash',
  hdrs = 'consistent_hash.h',
  srcs = 'consistent_hash.cc',
  deps = [
    '//flare/base:logging',
    '//flare/base:string',
  ]
)

cc_test(
  name = 'consistent_hash_test',
  srcs = 'consistent_hash_test.cc',
  deps = [
    ':consistent_hash',
    '//flare/base:random',
    '//yadcc/common:xxhash',
  ]
)

cc_library(
  name = 'in_memory_cache',
  hdrs = 'in_memory_cache.h',
  srcs = 'in_memory_cache.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:string',
  ]
)

cc_test(
  name = 'in_memory_cache_test',
  srcs = 'in_memory_cache_test.cc',
  deps = [
    ':in_memory_cache',
    '//flare/base:random',
    '//flare/base:string',
  ]
)

cc_library(
  name = 'cache_engine',
  hdrs = 'cache_engine.h',
  srcs = 'cache_engine.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:dependency_registry',
    '//thirdparty/jsoncpp:jsoncpp',
  ]
)

cc_library(
  name = 'null_cache_engine',
  hdrs = 'null_cache_engine.h',
  srcs = 'null_cache_engine.cc',
  deps = [
    ':cache_engine',
    '//flare/base:buffer',
  ],
  link_all_symbols = True
)

cc_library(
  name = 'disk_cache_engine',
  hdrs = 'disk_cache_engine.h',
  srcs = 'disk_cache_engine.cc',
  deps = [
    ':cache_engine',
    ':consistent_hash',
    ':dir',
    '//flare/base:buffer',
    '//flare/base:encoding',
    '//flare/base:handle',
    '//flare/base:string',
    '//flare/base/crypto:blake3',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/common:io',
    '//yadcc/common:xxhash',
  ],
  link_all_symbols = True
)

cc_test(
  name = 'disk_cache_engine_test',
  srcs = 'disk_cache_engine_test.cc',
  deps = [
    ':dir',
    ':disk_cache_engine',
    '//flare/base:random',
    '//flare/base:string',
    '//flare/init:override_flag',
    '//thirdparty/gflags:gflags',
  ]
)

cc_binary(
  name = 'yadcc-cache',
  srcs = 'entry.cc',
  deps = [
    ':cache_service_impl',
    ':disk_cache_engine',
    ':null_cache_engine',
    '//flare:init',
    '//flare/init:override_flag',
    '//flare/rpc:rpc',
    '//thirdparty/gperftools:tcmalloc_and_profiler',
    '//yadcc/common:inspect_auth',
  ]
)
