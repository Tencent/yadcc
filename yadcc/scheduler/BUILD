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
  name = 'task_dispatcher',
  hdrs = 'task_dispatcher.h',
  srcs = 'task_dispatcher.cc',
  deps = [
    ':running_task_bookkeeper',
    '//flare/base:expected',
    '//flare/base:exposed_var',
    '//flare/base:logging',
    '//flare/base:never_destroyed',
    '//flare/base:ref_ptr',
    '//flare/base:string',
    '//flare/base/internal:time_view',  # TODO(luobogao): Move it out.
    '//flare/fiber:fiber',
    '//thirdparty/gflags:gflags',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/api:env_desc_proto',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/api:scheduler_proto',
    '//yadcc/common:parse_size',
  ]
)

cc_test(
  name = 'task_dispatcher_test',
  srcs = 'task_dispatcher_test.cc',
  deps = [
    ':task_dispatcher',
    '//flare/testing:main',
    '//thirdparty/googletest:gmock',
  ]
)

cc_library(
  name = 'scheduler_service_impl',
  hdrs = 'scheduler_service_impl.h',
  srcs = 'scheduler_service_impl.cc',
  deps = [
    ':task_dispatcher',
    '//flare/base:compression',
    '//flare/base:encoding',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//thirdparty/openssl:crypto',
    '//yadcc/api:scheduler_proto_flare',
    '//yadcc/common:token_verifier',
  ]
)

cc_test(
  name = 'scheduler_service_impl_test',
  srcs = 'scheduler_service_impl_test.cc',
  deps = [
    ':scheduler_service_impl',
    '//flare/fiber:fiber',
    '//flare/init:override_flag',
    '//flare/rpc:rpc',
    '//flare/testing:main',
    '//flare/testing:rpc_controller',
    '//yadcc/scheduler:task_dispatcher',
  ]
)

cc_library(
  name = 'running_task_bookkeeper',
  hdrs = 'running_task_bookkeeper.h',
  srcs = 'running_task_bookkeeper.cc',
  deps = [
    '//yadcc/api:scheduler_proto',
  ]
)

cc_test(
  name = 'running_task_bookkeeper_test',
  srcs = 'running_task_bookkeeper_test.cc',
  deps = [
    ':running_task_bookkeeper',
    '//thirdparty/googletest:gmock',
  ]
)


cc_binary(
  name = 'yadcc-scheduler',
  srcs = [
    'entry.cc',
  ],
  deps = [
    ':scheduler_service_impl',
    ':task_dispatcher',
    '//flare:init',
    '//flare/init:override_flag',
    '//flare/rpc:rpc',
    '//thirdparty/gflags:gflags',
    '//thirdparty/gperftools:tcmalloc_and_profiler',
    '//yadcc/common:inspect_auth',
  ]
)
