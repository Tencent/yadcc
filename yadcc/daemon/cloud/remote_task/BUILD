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
  name = 'cxx_compilation_task',
  hdrs = 'cxx_compilation_task.h',
  srcs = 'cxx_compilation_task.cc',
  deps = [
    '//flare/base:compression',
    '//flare/base:future',
    '//flare/base:status',
    '//flare/base:string',
    '//flare/base/buffer:view',
    '//flare/base/crypto:blake3',
    '//flare/base/encoding:hex',
    '//flare/fiber:fiber',
    '//yadcc/api:daemon_proto',
    '//yadcc/api:extra_info_proto',
    '//yadcc/common:dir',
    '//yadcc/common:io',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:task_digest',
    '//yadcc/daemon:temp_dir',
    '//yadcc/daemon/cloud:compiler_registry',
    '//yadcc/daemon/cloud:remote_task',
    '//yadcc/daemon/cloud:temporary_dir',
  ],
  visibility = '//yadcc/daemon/cloud/...',
)

cc_test(
  name = 'cxx_compilation_task_test',
  srcs = 'cxx_compilation_task_test.cc',
  deps = [
    ':cxx_compilation_task',
    '//flare/base:compression',
    '//flare/base:string',
    '//flare/base/crypto:blake3',
    '//flare/base/encoding:hex',
    '//flare/testing:hooking_mock',
    '//flare/testing:main',
    '//yadcc/api:extra_info_proto',
    '//yadcc/common:io',
    '//yadcc/daemon:cache_format',
    '//yadcc/daemon:task_digest',
    '//yadcc/daemon/cloud:compiler_registry',
  ]
)
