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
  name = 'logging',
  hdrs = 'logging.h',
  srcs = 'logging.cc',
  deps = [
    '//thirdparty/fmt:fmt',
  ]
)

cc_library(
  name = 'span',
  hdrs = 'span.h',
)

cc_library(
  name = 'output_stream',
  hdrs = 'output_stream.h',
  srcs = 'output_stream.cc',
  deps = [
    ':logging',
    '//thirdparty/blake3:blake3',
    '//thirdparty/zstd:zstd',
    '#pthread',
  ]
)

cc_test(
  name = 'output_stream_test',
  srcs = 'output_stream_test.cc',
  deps = [
    ':output_stream',
    '//flare/base:logging',
    '//flare/base:random',
  ]
)

cc_library(
  name = 'compiler_args',
  hdrs = 'compiler_args.h',
  srcs = 'compiler_args.cc',
  deps = [
    ':escape',
    ':logging',
    ':span',
    ':utility',
  ]
)

cc_test(
  name = 'compiler_args_test',
  srcs = 'compiler_args_test.cc',
  deps = [
    ':compiler_args',
  ]
)

cc_library(
  name = 'compilation_saas',
  hdrs = 'compilation_saas.h',
  srcs = 'compilation_saas.cc',
  deps = [
    ':command',
    ':compiler_args',
    ':env_options',
    ':daemon_call',
    ':io',
    ':logging',
    ':rewrite_file',
    ':temporary_file',
    '//thirdparty/blake3:blake3',
    '//thirdparty/fmt:fmt',
    '//thirdparty/jsoncpp:jsoncpp',
    '//thirdparty/zstd:zstd',
  ]
)

cc_library(
  name = 'escape',
  hdrs = 'escape.h',
  srcs = 'escape.cc',
)

cc_test(
  name = 'escape_test',
  srcs = 'escape_test.cc',
  deps = [
    ':escape',
  ]
)

cc_library(
  name = 'command',
  hdrs = 'command.h',
  srcs = 'command.cc',
  deps = [
    ':compiler_args',
    ':io',
    ':logging',
    ':output_stream',
  ]
)

cc_test(
  name = 'command_test',
  srcs = 'command_test.cc',
  deps = [
    ':command',
  ]
)

cc_library(
  name = 'daemon_call',
  hdrs = 'daemon_call.h',
  srcs = 'daemon_call.cc',
  deps = [
    ':env_options',
    ':io',
    ':logging',
    ':utility',
  ]
)

cc_test(
  name = 'daemon_call_test',
  srcs = 'daemon_call_test.cc',
  deps = [
    ':daemon_call',
    '//flare/base/net:endpoint',
    '//flare/fiber:fiber',
    '//flare/rpc:rpc',
    '//flare/testing:endpoint',
    '//flare/testing:main',
  ]
)

cc_library(
  name = 'io',
  hdrs = 'io.h',
  srcs = 'io.cc',
  deps = [
    ':logging',
  ]
)

cc_library(
  name = 'temporary_file',
  hdrs = 'temporary_file.h',
  srcs = 'temporary_file.cc',
  deps = [
    ':io',
    ':logging',
  ]
)

cc_test(
  name = 'temporary_file_test',
  srcs = 'temporary_file_test.cc',
  deps = [
    ':temporary_file',
  ]
)

cc_library(
  name = 'env_options',
  hdrs = 'env_options.h',
  srcs = 'env_options.cc',
  deps = [
    ':logging',
  ],
)

cc_test(
  name = 'env_options_test',
  srcs = 'env_options_test.cc',
  deps = [
    ':env_options',
  ]
)

cc_library(
  name = 'task_quota',
  hdrs = 'task_quota.h',
  srcs = 'task_quota.cc',
  deps = [
    ':env_options',
    ':daemon_call',
    ':logging',
    '//thirdparty/fmt:fmt',
  ],
)

cc_test(
  name = 'task_quota_test',
  srcs = 'task_quota_test.cc',
  deps = [
    ':daemon_call',
    ':task_quota',
    '//flare/testing:hooking_mock',
    '//thirdparty/googletest:gmock',
  ]
)

cc_library(
  name = 'libfakeroot',
  hdrs = 'libfakeroot.h',
  srcs = 'libfakeroot.cc',
  deps = [
    ':io',
    ':logging',
    '//thirdparty/fmt:fmt',
    '//yadcc/client/libfakeroot:libfakeroot',
    '#dl',
  ]
)

cc_test(
  name = 'libfakeroot_test',
  srcs = 'libfakeroot_test.cc',
  deps = [
    ':io',
    ':libfakeroot',
    '//yadcc/client/libfakeroot:libfakeroot',
  ]
)

cc_library(
  name = 'rewrite_file',
  hdrs = 'rewrite_file.h',
  srcs = 'rewrite_file.cc',
  deps = [
    ':command',
    ':compiler_args',
    ':env_options',
    ':libfakeroot',
    ':logging',
    ':output_stream',
    ':task_quota',
    ':utility',
  ]
)

cc_library(
  name = 'utility',
  hdrs = 'utility.h',
  srcs = 'utility.cc',
  deps = [
    ':logging',
  ]
)

cc_binary(
  name = 'yadcc',
  srcs = [
    'yadcc.cc',
  ],
  deps = [
    ':command',
    ':compilation_saas',
    ':compiler_args',
    ':env_options',
    ':escape',
    ':daemon_call',
    ':io',
    ':libfakeroot',
    ':logging',
    ':output_stream',
    ':rewrite_file',
    ':task_quota',
    ':temporary_file',
    ':utility',
    '//thirdparty/fmt:fmt',
    # Not using `tcmalloc` here. From my test (CentOS 7), initialization of
    # tcmalloc outweighs its benefit. We might want to re-evaluate the situation
    # periodically, though.
  ],
  extra_linkflags = ['-static'],
)

cc_binary(
  name = 'yadcc-universal-wrapper',
  srcs = [
    'universal_wrapper.cc',
  ],
  deps = [
    ':command',
    ':env_options',
    ':task_quota',
    ':utility',
  ]
)
