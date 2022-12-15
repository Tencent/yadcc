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

#include "yadcc/client/cxx/compiler_args.h"

#include <string_view>
#include <unordered_set>

#include "yadcc/client/common/escape.h"
#include "yadcc/client/common/logging.h"
#include "yadcc/client/common/span.h"
#include "yadcc/client/common/utility.h"

namespace yadcc::client {

namespace {

// Shamelessly copied from
// https://github.com/icecc/icecream/blob/master/client/arg.cpp.
const std::unordered_set<std::string_view> kOneValueArguments = {
    "-o",
    "-x",

    "-dyld-prefix",
    "-gcc-toolchain",
    "--param",
    "--sysroot",
    "--system-header-prefix",
    "-target",
    "--assert",
    "--allowable_client",
    "-arch",
    "-arch_only",
    "-arcmt-migrate-report-output",
    "--prefix",
    "-bundle_loader",
    "-dependency-dot",
    "-dependency-file",
    "-dylib_file",
    "-exported_symbols_list",
    "--bootclasspath",
    "--CLASSPATH",
    "--classpath",
    "--resource",
    "--encoding",
    "--extdirs",
    "-filelist",
    "-fmodule-implementation-of",
    "-fmodule-name",
    "-fmodules-user-build-path",
    "-fnew-alignment",
    "-force_load",
    "--output-class-directory",
    "-framework",
    "-frewrite-map-file",
    "-ftrapv-handler",
    "-image_base",
    "-init",
    "-install_name",
    "-lazy_framework",
    "-lazy_library",
    "-meabi",
    "-mhwdiv",
    "-mllvm",
    "-module-dependency-dir",
    "-mthread-model",
    "-multiply_defined",
    "-multiply_defined_unused",
    "-rpath",
    "--rtlib",
    "-seg_addr_table",
    "-seg_addr_table_filename",
    "-segs_read_only_addr",
    "-segs_read_write_addr",
    "-serialize-diagnostics",
    "--serialize-diagnostics",
    "-std",
    "--stdlib",
    "--force-link",
    "-umbrella",
    "-unexported_symbols_list",
    "-weak_library",
    "-weak_reference_mismatches",
    "-B",
    "-D",
    "-U",
    "-I",
    "-i",
    "--include-directory",
    "-L",
    "-l",
    "--library-directory",
    "-MF",
    "-MT",
    "-MQ",
    "-cxx-isystem",
    "-c-isystem",
    "-idirafter",
    "--include-directory-after",
    "-iframework",
    "-iframeworkwithsysroot",
    "-imacros",
    "-imultilib",
    "-iprefix",
    "--include-prefix",
    "-iquote",
    "-include",
    "-include-pch",
    "-isysroot",
    "-isystem",
    "-isystem-after",
    "-ivfsoverlay",
    "-iwithprefix",
    "--include-with-prefix",
    "--include-with-prefix-after",
    "-iwithprefixbefore",
    "--include-with-prefix-before",
    "-iwithsysroot"};

// TODO(luobogao): Handling arguments with two option values.

}  // namespace

CompilerArgs::CompilerArgs(int argc, const char** argv) {
  CHECK(argc >= 1);
  for (int i = 0; i != argc; ++i) {
    if (kOneValueArguments.count(argv[i])) {
      args_.emplace_back(argv[i], OptionArgs(&argv[i + 1], 1));
      ++i;
    } else {
      if (argv[i][0] == '-') {
        // This argument do not have a value.
        args_.emplace_back(argv[i], OptionArgs());
      } else {
        filenames_.push_back(argv[i]);
      }
    }
  }
  for (int i = 0; i != argc; ++i) {
    rebuilt_ += EscapeCommandArgument(argv[i]) + " ";
  }
  rebuilt_.pop_back();  // Trailing whitespace.
}

const CompilerArgs::OptionArgs* CompilerArgs::TryGet(
    const std::string_view& key) const {
  for (auto&& [k, v] : args_) {
    if (k == key) {
      return &v;
    }
  }
  return nullptr;
}

const CompilerArgs::OptionArgs* CompilerArgs::TryGetByPrefix(
    const std::string_view& prefix) const {
  for (auto&& [k, v] : args_) {
    if (StartsWith(k, prefix)) {
      return &v;
    }
  }
  return nullptr;
}

std::string CompilerArgs::GetOutputFile() const {
  std::string target;
  if (auto opt = TryGet("-o")) {
    return opt->front();
  } else {
    CHECK(GetFilenames().size() == 1);
    std::string_view filename = GetFilenames()[0];
    auto path = filename.substr(0, filename.find_last_of('.'));
    if (auto pos = path.find_last_of('/')) {
      path = path.substr(pos + 1);
    }
    return std::string(path) + ".o";
  }
}

RewrittenArgs CompilerArgs::Rewrite(
    const std::unordered_set<std::string_view>& remove,
    const std::vector<std::string_view>& remove_prefix,
    const std::initializer_list<std::string_view>& add,
    bool keep_filenames) const {
  std::vector<std::string> result;
  for (auto&& [k, v] : args_) {
    if (remove.count(k)) {
      continue;
    }
    bool skip = false;
    for (auto&& e : remove_prefix) {
      if (StartsWith(k, e)) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }
    result.push_back(k);
    for (auto&& e : v) {
      result.push_back(e);
    }
  }
  for (auto&& e : add) {
    result.push_back(std::string(e));
  }
  if (keep_filenames) {
    for (auto&& e : filenames_) {
      result.push_back(e);
    }
  }
  return RewrittenArgs(compiler_, result);
}

}  // namespace yadcc::client
