# Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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
  name = 'user_task',
  hdrs = 'user_task.h',
  deps = [
    '//flare/base:expected',
    '//flare/base:status',
    '//yadcc/daemon/local:distributed_task',
  ],
)

cc_library(
  name = 'cxx_compilation_task',
  hdrs = 'cxx_compilation_task.h',
  srcs = 'cxx_compilation_task.cc',
  deps = [
    ':user_task',
    '//flare/base:buffer',
    '//flare/base:string',
    '//flare/net/http:types',
    '//flare/rpc:rpc',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:extra_info_proto',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:task_digest',
    '//yadcc/daemon/local:file_digest_cache',
    '//yadcc/daemon/local:messages_proto',
  ],
  visibility = '//yadcc/daemon/local:...',
)

cc_test(
  name = 'cxx_compilation_task_test',
  srcs = 'cxx_compilation_task_test.cc',
  deps = [
    ':cxx_compilation_task',
    '//flare/base/buffer:packing',
    '//flare/testing:hooking_mock',
    '//flare/testing:main',
    '//flare/testing:rpc_mock',
    '//yadcc/api:daemon_proto_flare',
    '//yadcc/api:extra_info_proto',
    '//yadcc/common:io',
    '//yadcc/daemon/local:file_digest_cache',
    '//yadcc/daemon/local:messages_proto',
  ]
)
