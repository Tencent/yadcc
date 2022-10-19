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
  name = 'fakeroot',
  hdrs = [],
  srcs = 'fakeroot.c',
  deps = [
    '#dl'
  ],
  visibility = '//yadcc/client/...',
)

# Serializes `libfakeroot.so` into `libfakeroot.h`, so that we can later embed
# that shared library into `yadcc-client`.
#
# Ideally we should store `libfakeroot.so` in `libfakeroot.c` and only leave a
# declaration (but not a definition) in header. However, since we only include
# it once, for the sake of rule's simplicity, we store everything in header.
gen_rule(
  name = 'libfakeroot',
  outs = 'libfakeroot.h',
  cmd = 'cd $BUILD_DIR && xxd -i $SRC_DIR/libfakeroot.so ../$FIRST_OUT',
  deps = [
    ':fakeroot'
  ],
  visibility = '//yadcc/client/...',
)
