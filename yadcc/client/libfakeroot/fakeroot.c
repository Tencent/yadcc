// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this
// file except in compliance with the License. You may obtain a copy of the
// License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

// Rewrites absolute path of GCC's standary library path to a "fake root". This
// helps you to make content of preprocessed source code irrelevant to the
// location of the compiler.
//
// We don't take care of CWD. Get ride of that using `-fno-working-directory`
// yourself.

#define _GNU_SOURCE

#include <dlfcn.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char kFakeRoot[] = "/yadcc/compiler";

typedef struct {
  const char* path;
  size_t size;
} SelfPathDesc;

typedef int (*PfnFprintf)(FILE* stream, const char* format, ...);

PfnFprintf GetOriginalPrintf() {
  static PfnFprintf orig_fprintf;
  if (!orig_fprintf) {
    orig_fprintf = (PfnFprintf)dlsym(RTLD_NEXT, "fprintf");
  }
  return orig_fprintf;
}

SelfPathDesc GetSelfPath() {
  static SelfPathDesc desc;
  if (!desc.path) {
    // Provided by yadcc client if it wants us to rewrite compiler path.
    desc.path = getenv("YADCC_INTERNAL_COMPILER_PATH");
    desc.size = desc.path ? strlen(desc.path) : 0;
  }
  return desc;
}

// We're expected to be loaded via `LD_PRELOAD`. Therefore, because of "symbol
// interposing", all calls to `fprintf` should be direct to us.
int fprintf(FILE* stream, const char* format, ...) {
  int result;

  va_list args;
  va_start(args, format);

  // We only take care of "linemarkers".
  //
  // @sa: https://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html

  // Not a linemarker.
  if (strcmp(format, "# %u \"%s\"%s")) {
    result = vfprintf(stream, format, args);
    goto out;
  }

  // Copy the args as we're going to inspect it.
  va_list args_copy;
  va_copy(args_copy, args);

  int line = va_arg(args_copy, int);
  const char* path = va_arg(args_copy, const char*);
  const char* extra = va_arg(args_copy, const char*);

  // Replace any references back to compiler's path with our fake root.
  //
  // /opt/gcc/lib/gcc/x86_64-pc-linux-gnu/8/include
  // /opt/gcc/lib/gcc/x86_64-pc-linux-gnu/8/include-fixed
  // /opt/gcc/include
  // ...
  SelfPathDesc self_path = GetSelfPath();
  int path_length = strlen(path);

  if (path_length >= PATH_MAX /* How can it be? */ ||
      self_path.size == 0 /* We shouldn't patch the path then. */ ||
      path_length < self_path.size ||
      memcmp(path, self_path.path, self_path.size)) {
    // The path does not reference us, let it go then.
    result = vfprintf(stream, format, args);
    goto out2;
  }

  // Now replace the path prefix.
  const char* end_of_prefix = path + self_path.size;
  size_t rest_size = path_length - (end_of_prefix - path);
  char temp_buffer[PATH_MAX + 64 /* Space for our fake root. */];
  memcpy(temp_buffer, kFakeRoot, sizeof(kFakeRoot));  // Our fake root.
  memcpy(temp_buffer + sizeof(kFakeRoot) - 1 /* Terminating null. */,
         end_of_prefix,
         rest_size);  // Rest of the path.
  temp_buffer[sizeof(kFakeRoot) - 1 + rest_size] = 0;
  result = GetOriginalPrintf()(stream, format, line, temp_buffer, extra);
  goto out2;

out2:
  va_end(args_copy);

out:
  va_end(args);

  return result;
}
