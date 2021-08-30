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
  name = 'compiler_args',
  hdrs = 'compiler_args.h',
  srcs = 'compiler_args.cc',
  deps = [
    '//yadcc/client/common:escape',
    '//yadcc/client/common:logging',
    '//yadcc/client/common:rewritten_args',
    '//yadcc/client/common:span',
    '//yadcc/client/common:utility',
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
    ':compiler_args',
    ':rewrite_file',
    '//thirdparty/blake3:blake3',
    '//thirdparty/fmt:fmt',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/client/common:command',
    '//yadcc/client/common:compress',
    '//yadcc/client/common:daemon_call',
    '//yadcc/client/common:env_options',
    '//yadcc/client/common:io',
    '//yadcc/client/common:logging',
    '//yadcc/client/common:multi_chunk',
    '//yadcc/client/common:temporary_file',
    '//yadcc/client/common:utility',
  ]
)

cc_test(
  name = 'compilation_saas_test',
  srcs = 'compilation_saas_test.cc',
  deps = [
    ':compilation_saas',
    '//thirdparty/googletest:gmock',
    '//thirdparty/jsoncpp:jsoncpp',
    '//yadcc/client/common:compress',
    '//yadcc/client/common:daemon_call',
    '//yadcc/client/common:logging',
    '//yadcc/client/common:multi_chunk',
    '//yadcc/client/cxx:compilation_saas',
  ],
  testdata = [
    ('testdata', 'testdata'),
  ]
)

cc_library(
  name = 'libfakeroot',
  hdrs = 'libfakeroot.h',
  srcs = 'libfakeroot.cc',
  deps = [
    '//thirdparty/fmt:fmt',
    '//yadcc/client/common:io',
    '//yadcc/client/common:logging',
    '//yadcc/client/cxx/libfakeroot:libfakeroot',
    '#dl',
  ]
)

cc_test(
  name = 'libfakeroot_test',
  srcs = 'libfakeroot_test.cc',
  deps = [
    ':libfakeroot',
    '//yadcc/client/common:io',
    '//yadcc/client/cxx/libfakeroot:libfakeroot',
  ]
)

cc_library(
  name = 'rewrite_file',
  hdrs = 'rewrite_file.h',
  srcs = 'rewrite_file.cc',
  deps = [
    ':compiler_args',
    ':libfakeroot',
    '//yadcc/client/common:command',
    '//yadcc/client/common:env_options',
    '//yadcc/client/common:logging',
    '//yadcc/client/common:output_stream',
    '//yadcc/client/common:task_quota',
    '//yadcc/client/common:utility',
  ]
)

cc_binary(
  name = 'yadcc-cxx',
  srcs = 'yadcc-cxx.cc',
  deps = [
    ':compilation_saas',
    ':compiler_args',
    ':libfakeroot',
    ':rewrite_file',
    '//thirdparty/fmt:fmt',
    '//yadcc/client/common:command',
    '//yadcc/client/common:daemon_call',
    '//yadcc/client/common:env_options',
    '//yadcc/client/common:escape',
    '//yadcc/client/common:io',
    '//yadcc/client/common:logging',
    '//yadcc/client/common:output_stream',
    '//yadcc/client/common:task_quota',
    '//yadcc/client/common:temporary_file',
    '//yadcc/client/common:utility',
    # Not using `tcmalloc` here. From my test (CentOS 7), initialization of
    # tcmalloc outweighs its benefit. We might want to re-evaluate the situation
    # periodically, though.
  ],
  # Avoids overhead of dynamic linker.
  extra_linkflags = ['-static'],
)
