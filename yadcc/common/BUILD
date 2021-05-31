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
  name = 'io',
  hdrs = 'io.h',
  srcs = 'io.cc',
  deps = [
    '//flare/base:buffer',
    '//flare/base:logging',
  ],
  visibility = 'PUBLIC',
)

cc_test(
  name = 'io_test',
  srcs = 'io_test.cc',
  deps = [
    ':io',
  ]
)

cc_library(
  name = 'xxhash',
  hdrs = 'xxhash.h',
  srcs = 'xxhash.cc',
  deps = [
    '//thirdparty/xxhash:xxhash',
  ],
  visibility = 'PUBLIC',
)

cc_test(
  name = 'xxhash_test',
  srcs = 'xxhash_test.cc',
  deps = [
    ':xxhash',
    '//thirdparty/xxhash:xxhash',
  ]
)

cc_library(
  name = 'token_verifier',
  hdrs = 'token_verifier.h',
  srcs = 'token_verifier.cc',
  deps = [
    '//flare/base:logging',
    '//flare/base:string',
    '//thirdparty/gflags:gflags',
  ],
  visibility = 'PUBLIC',
)

cc_test(
  name = 'token_verifier_test',
  srcs = 'token_verifier_test.cc',
  deps = [
    ':token_verifier',
    '//thirdparty/gflags:gflags',
  ]
)

cc_library(
  name = 'inspect_auth',
  hdrs = 'inspect_auth.h',
  srcs = 'inspect_auth.cc',
  deps = [
    '//flare/base:string',
    '//flare/rpc/builtin:basic_authentication_http_filter',
    '//thirdparty/gflags:gflags',
  ],
  visibility = 'PUBLIC',
)

cc_test(
  name = 'inspect_auth_test',
  srcs = 'inspect_auth_test.cc',
  deps = [
    ':inspect_auth',
    '//flare/base:encoding',
    '//flare/base:string',
    '//flare/net/http:http',
    '//thirdparty/gflags:gflags',
  ]
)

cc_library(
  name = 'parse_size',
  hdrs = 'parse_size.h',
  srcs = 'parse_size.cc',
  deps = [
    '//flare/base:logging',
    '//flare/base:string',
  ],
  visibility = 'PUBLIC',
)

cc_test(
  name = 'parse_size_test',
  srcs = 'parse_size_test.cc',
  deps = [
    ':parse_size',
  ]
)
