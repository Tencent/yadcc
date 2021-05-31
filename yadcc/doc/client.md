# 客户端

[客户端](../client)主要负责调用编译器进行预处理并压缩，并将预处理的结果及其他一些信息（如源代码的[BLAKE3](https://github.com/BLAKE3-team/BLAKE3)哈希）传递给[守护进程](daemon.md)进行编译。

## 配置

另外，出于调试等目的，我们提供了如下参数控制客户端的行为：

*部分工具链（如[Bazel](https://bazel.build/designs/2016/06/21/environment.html#current-shortcomings)）会在调用编译器之前“重置”环境变量。此时需要用户自行采取工具链特有的方式传递如下配置项给`yadcc`。*

- `YADCC_COMPILE_ON_CLOUD_SIZE_THRESHOLD`：整数类型。预处理后（并zstd压缩后）大小小于这一配置项的文件会本地编译。

- `YADCC_WARN_ON_WAIT_LONGER_THAN`：整数类型。如果在等待了这么多秒之后本地守护进程依然没有授予wrapper开始执行命令的权力，会输出一条warning级别日志。

- `YADCC_CACHE_CONTROL`：控制缓存行为，可选参数：

  - `0`：禁用缓存
  - `1`：启用缓存（默认值）
  - `2`：不使用缓存，但用编译结果更新已有缓存。这一选项通常用于调试目的。

- `YADCC_LOG_LEVEL`：日志级别。0~5对应`DEBUG` / `TRACE` / `INFO` / `WARN` / `ERROR`。

- `YADCC_DAEMON_PORT`：本地守护进程的监听端口，默认`8334`。

- `YADCC_IGNORE_TIMESTAMP_MACROS`：如果配置为1，则`yadcc`不检查源代码中的[`__TIME__`](https://en.cppreference.com/w/c/preprocessor/replace) / [`__DATE__`](https://en.cppreference.com/w/c/preprocessor/replace) / [`__TIMESTAMP__`](https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html)。

  默认情况下，`yadcc`会扫描预处理后的代码并检查有没有这些宏。如果存在这些宏的话，实际上编译结果每次会发生变化（时间戳变动），因此`yadcc`会跳过编译缓存并编译。

  但是**这一扫描（内部目前依赖于[`memmem`](https://man7.org/linux/man-pages/man3/memmem.3.html)）速度（相对而言）非常慢**，在我们的测试中开销甚至超过了对预处理结果的压缩。

  同时，现代化的编译工具链通常会提供高效且统一的途径获取包括但不限于时间戳（如[Blade的`scm.c`](https://github.com/chen3feng/blade-build/blob/master/src/blade/builtin_tools.py#L72)）等信息。也有一些工具链会更进一步，直接屏蔽这些宏（如[Bazel的`determinism`](https://github.com/bazelbuild/bazel/blob/master/tools/cpp/windows_cc_toolchain_config.bzl#L292)）。

  因此在实际生产环境中**我们建议代码不要依赖这些宏，并开启这一选项**。

  *`yadcc`如果检测到编译参数中已经通过`-D...=...`屏蔽了这些宏（三个宏都被屏蔽时），那么会默认开启这一选项。*

- `YADCC_COMPILE_ON_PRIVATE_CLOUD`：如果配置为1，则会在本地编译。用于调试目的。

- `YADCC_WARN_ON_NONCACHEABLE`：如果配置为1，遇到无法缓存的源代码（通常是`__TIME__`等宏导致）会输出一条警告。用于调试目的。

## 优化

出于编译性能考虑，我们在客户端中作了如下一些优化：

- 预处理完后足够小的文件本地编译：通常（但并不绝对）预处理完之后如果文件较小，本地编译的开销可能不足以覆盖分布式编译的网络等开销。这种情况下我们会直接在本地进行编译（并由守护进程控制并发度）。

- 我们使用GCC参数`-fdirectives-only`加速预处理，并在特殊情况下回落至普通的`-E`。

  考虑到分布式编译（除链接外）本地主要工作负载在于预处理，因此我们通常很关注预处理的效率。

  经过我们对比，对于`.proto`繁重的TU，二者的差异可以达到0.5s vs 2s的区别。这通常也是分布式编译（不考虑链接）的本地瓶颈所在。

  *以8C机器为例。单核每2s（`-E`）产出一个预处理后的文件意味着每秒只可以提交4个任务，而0.5s（`-fdirectives-only`）产出一个文件则每秒可以提交16个任务。*

  但是这一参数实际上具有其自身的局限性。这个参数对部分（但非全部）使用`__COUNTER__`的代码无法正常处理。例如在我们代码库中使用较多的[Google Benchmark](https://github.com/google/benchmark)的头文件[benchmark.h](https://github.com/google/benchmark/blob/master/include/benchmark/benchmark.h#L1098)即存在这一参数无法处理的代码。由于其存在于头文件中，这意味着我们几乎所有的benchmark代码在启用这一参数时都无法编译。

  通常来说，为了保证代码可干净的编译，我们只能全部使用普通的`-E`处理（否则会预处理失败，取决于实现可能导致编译失败或输出错误并回落本地编译这一TU）。这很大程度上的影响了分布式编译的效率（见前文对比）。

  这儿我们从实际场景出发，优先尝试`-fdirectives-only`预处理，预处理失败时静默回落至`-E`。这样，我们同时兼顾了（大多数成功的）高效场景和正确性。

- 特别的，针对广告线的开发环境（各个机器的GCC通常手动安装至可能不同的路径），还存在编译器路径不同导致预处理结果不同的问题。这会影响缓存命中率。针对这一问题，我们作了[一些优化](../client/libfakeroot/fakeroot.c)，统一化了预处理结果中的路径。这样预处理后的哈希（不考虑系统头文件不同的场景）即可保证不同机器上一致、对编译器安装路径不敏感并命中缓存。

- 使用性能更好的（密码学）哈希、压缩算法减少本地CPU占用，将CPU尽可能交给GCC做预处理，提升吞吐。

  这主要在于如下几方面：

  - 密码学哈希：我们使用[BLAKE3](https://github.com/BLAKE3-team/BLAKE3)（密码学哈希）计算预处理后的摘要（主要用于缓存）。

    和大多数人直觉不同，对于GCC预处理结果到编译结果的缓存的场景中，计算预处理结果的哈希可能会占据不可忽略的一部分CPU时间。

    这儿主要的问题在于GCC预处理（使用`-fdirectives-only`）的效率非常高。

    在我们的测试中，`.proto`繁重的代码，GCC 8.2预处理生成输出的速度大约在100MB/s（单个进程/线程）。而作为对比，在Skylake上（单线程、CentOS7自带的OpenSSL 1.0.2k，8K块）：

    - MD5大约为550MB/s
    - SHA1大约为776MB/s

    换句话说，使用MD5会引入额外约20%的CPU开销，SHA1会引入约14%的开销。

    在我们的环境中，BLAKE3可以提供~3GB/s的哈希效率，留下了更多的CPU时间给GCC做预处理，进一步提升吞吐。

  - 压缩：我们使用时空表现出色的[zstd](https://github.com/facebook/zstd)算法。相对于传统选择LZO，zstd允许我们将更多的时间交给GCC作预处理。

  注：与我们一样，[ccache 4.0](https://ccache.dev/releasenotes.html#_ccache_4_0)开始也选择切换至blake3 + zstd + xxhash。（xxhash我们主要在[本地守护进程](daemon.md)管理缓存布隆过滤器使用。）
