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

#include "yadcc/client/cxx/rewrite_file.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "yadcc/client/common/command.h"
#include "yadcc/client/common/env_options.h"
#include "yadcc/client/common/logging.h"
#include "yadcc/client/common/output_stream.h"
#include "yadcc/client/common/span.h"
#include "yadcc/client/common/task_quota.h"
#include "yadcc/client/common/utility.h"
#include "yadcc/client/cxx/compiler_args.h"
#include "yadcc/client/cxx/libfakeroot.h"

namespace yadcc::client {

std::optional<std::string> DetermineProgramLanguage(const CompilerArgs& args) {
  if (auto opt = args.TryGet("-x")) {
    return std::string(opt->front());
  }
  // Checked by `IsCompilerInvocationDistributable()`.
  CHECK(args.GetFilenames().size() == 1);
  auto filename = args.GetFilenames()[0];
  if (EndsWith(filename, ".cc") || EndsWith(filename, ".cpp") ||
      EndsWith(filename, ".cxx") /* ... */) {
    return "c++";
  } else if (EndsWith(filename, ".c")) {
    return "c";
  }
  LOG_TRACE(
      "Failed to determine program language from arguments. Invoked with: {}",
      args.Rebuild());
  return std::nullopt;
}

std::string_view GetCompilerPathIfNeedsPatch(const CompilerArgs& args) {
  if (args.TryGet("--coverage") || args.TryGet("--ftest-coverage")) {
    // Compiler's path does matter if coverage is enabled.
    return "";
  }
  std::string_view view = args.GetCompiler();
  auto pos = view.find_last_of('/');
  view = view.substr(0, pos);
  if (StartsWith(view, "/usr/bin/") || StartsWith(view, "/opt/rh/")) {
    // The compiler should always be installed to the same path then. Don't
    // patch it.
    return "";
  }
  if (!EndsWith(view, "/bin")) {  // Unexpected path, don't patch it.
    return "";
  }
  return view.substr(0, view.size() - 4 /* "/bin" */);
}

// Returns: `zstd_written`, `cache_control`, `digest` (applicable only if
// caching is allowed.)
std::optional<std::tuple<std::string, CacheControl, std::string>>
TryRewriteFileWithCommandLine(const CompilerArgs& args,
                              const RewrittenArgs& cmdline,
                              CacheControl cache_control) {
  // TODO(luobogao): This trick is not required if system-installed (or RHEL
  // devtoolset) is used. We should detect system environment and only enable
  // this trick conditionally.
  static auto kEnvPreload = fmt::format("LD_PRELOAD={}", GetLibFakeRootPath());
  static auto kEnvCompilerPath = fmt::format("YADCC_INTERNAL_COMPILER_PATH={}",
                                             GetCompilerPathIfNeedsPatch(args));

  std::array<OutputStream*, 8> streams;
  std::size_t num_streams = 0;

  // Preprocessed source code compressed well, it's plain text.
  ZstdCompressedOutputStream zstd_os;
  streams[num_streams++] = &zstd_os;

  // We don't initialize this stream unless caching is enabled.
  std::optional<Blake3OutputStream> cache_params_os;
  if (cache_control != CacheControl::Disallow) {
    cache_params_os = Blake3OutputStream();
    streams[num_streams++] = &*cache_params_os;
  }

  [[maybe_unused]] std::string error;  // Errors are actually dropped.
  ForwardingOutputStream output(Span(streams.data(), num_streams));
  auto ec = ExecuteCommand(cmdline, {kEnvPreload, kEnvCompilerPath}, "",
                           &output, &error);

  if (ec == 0) {
    // For the moment we don't have extra test to perform to determine if the
    // task can be cached. Therefore, `cache_control` is always respected.
    //
    // Side note: Testing for non-cacheable macros is done by compile-servers.
    auto resulting_cache_control = cache_control;
    std::string source_digest;
    if (cache_params_os) {
      cache_params_os->Finalize();
      source_digest = cache_params_os->GetSourceDigest();
    }
    return std::tuple(zstd_os.FlushAndGet(), resulting_cache_control,
                      source_digest);
  }
  return std::nullopt;
}

std::optional<RewriteResult> RewriteFile(const CompilerArgs& args) {
  // It's unlikely but if we can't determine source code language, bail out.
  auto language = DetermineProgramLanguage(args);
  if (!language) {
    return {};
  }
  auto cache_control = GetOptionCacheControl();
  CHECK(args.GetFilenames().size() == 1);  // Otherwise we shouldn't get here.

  // FIXME: Should we lower process priority for lightweight tasks? CPU
  // resources are already over-provisioned to them.
  auto quota = AcquireTaskQuota(true);

  // Even if `-fdirectives-only` does not work correctly all the time, it DOES
  // work most of the time. In case it works, it performs much better than `-E`.
  //
  // For our codebase, it's beneficial to try `-fdirectives-only` first and fall
  // back to `-E` only if that does not work.
  {
    auto cmdline = args.Rewrite(
        // Do NOT emit CWD in the preprocessed file, otherwise compilation won't
        // work unless everyone compiles in the same directory.
        {"-c", "-o", "-fworking-directory"}, {},
        {"-fno-working-directory", "-E", "-fdirectives-only"}, true);
    if (auto opt =
            TryRewriteFileWithCommandLine(args, cmdline, cache_control)) {
      auto&& [rewritten, cache_control, digest] = *opt;
      return RewriteResult{.language = *language,
                           .source_path = args.GetFilenames()[0],
                           .directives_only = true,
                           .cache_control = cache_control,
                           .zstd_rewritten = std::move(rewritten),
                           .source_digest = std::move(digest)};
    }
    // FIXME: Or should we use INFO log here? Perhaps we can log compiler
    // invocations without `-fdirectives-only` at daemon side and report that to
    // scheduler?
    LOG_TRACE(
        "Failed to rewrite source file with `-fdirectives-only`, retrying with "
        "`-E`.");
  }

  // Fallback to `-E` mode then.
  {
    auto cmdline = args.Rewrite({"-c", "-o", "-fworking-directory"}, {},
                                {"-fno-working-directory", "-E"}, true);
    if (auto opt =
            TryRewriteFileWithCommandLine(args, cmdline, cache_control)) {
      auto&& [rewritten, cache_control, digest] = *opt;
      return RewriteResult{.language = *language,
                           .source_path = args.GetFilenames()[0],
                           .directives_only = true,
                           .cache_control = cache_control,
                           .zstd_rewritten = std::move(rewritten),
                           .source_digest = std::move(digest)};
    }
    LOG_TRACE("Failed to rewrite source file. Invoked with: [{}]",
              args.Rebuild());
    return std::nullopt;
  }
}

}  // namespace yadcc::client
