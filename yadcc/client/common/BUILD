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
  name = 'output_stream',
  hdrs = 'output_stream.h',
  srcs = 'output_stream.cc',
  deps = [
    ':logging',
    ':span',
    '//thirdparty/blake3:blake3',
    '//thirdparty/zstd:zstd',
    '#pthread',
  ],
  visibility = '//yadcc/client/...',
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
  name = 'logging',
  hdrs = 'logging.h',
  srcs = 'logging.cc',
  deps = [
    '//thirdparty/fmt:fmt',
  ],
  visibility = '//yadcc/client/...',
)

cc_library(
  name = 'span',
  hdrs = 'span.h',
  visibility = '//yadcc/client/...',
)

cc_library(
  name = 'escape',
  hdrs = 'escape.h',
  srcs = 'escape.cc',
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'escape_test',
  srcs = 'escape_test.cc',
  deps = [
    ':escape',
  ]
)

cc_library(
  name = 'rewritten_args',
  hdrs = 'rewritten_args.h',
  srcs = 'rewritten_args.cc',
  deps = [
    ':escape',
    ':span',
  ],
  visibility = '//yadcc/client/...',
)

cc_library(
  name = 'command',
  hdrs = 'command.h',
  srcs = 'command.cc',
  deps = [
    ':io',
    ':logging',
    ':output_stream',
    ':rewritten_args',
  ],
  visibility = '//yadcc/client/...',
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
  ],
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'daemon_call_test',
  srcs = 'daemon_call_test.cc',
  deps = [
    ':daemon_call',
    '//flare/base:random',
    '//flare/base:string',
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
    '//thirdparty/fmt:fmt',
  ],
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'io_test',
  srcs = 'io_test.cc',
  deps = [
    ':io',
  ]
)

cc_library(
  name = 'temporary_file',
  hdrs = 'temporary_file.h',
  srcs = 'temporary_file.cc',
  deps = [
    ':io',
    ':logging',
  ],
  visibility = '//yadcc/client/...',
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
  visibility = '//yadcc/client/...',
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
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'task_quota_test',
  srcs = 'task_quota_test.cc',
  deps = [
    ':daemon_call',
    ':task_quota',
    '//thirdparty/googletest:gmock',
  ]
)

cc_library(
  name = 'utility',
  hdrs = 'utility.h',
  srcs = 'utility.cc',
  deps = [
    ':io',
    ':logging',
    '//thirdparty/blake3:blake3',
  ],
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'utility_test',
  srcs = 'utility_test.cc',
  deps = [
    ':utility',
  ]
)

cc_library(
  name = 'multi_chunk',
  hdrs = 'multi_chunk.h',
  srcs = 'multi_chunk.cc',
  deps = [
    ':utility',
  ],
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'multi_chunk_test',
  srcs = 'multi_chunk_test.cc',
  deps = [
    ':multi_chunk',
  ]
)

cc_library(
  name = 'compress',
  hdrs = 'compress.h',
  srcs = 'compress.cc',
  deps = [
    ':logging',
    '//thirdparty/zstd:zstd',
  ],
  visibility = '//yadcc/client/...',
)

cc_test(
  name = 'compress_test',
  srcs = 'compress_test.cc',
  deps = [
    ':compress',
  ]
)
