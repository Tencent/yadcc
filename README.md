# Yadcc 分布式 C++ 编译器

`Yadcc`是一套腾讯广告自研的工业级C++分布式编译系统。目前在我们1700+核的集群中每天编译300,0000+个目标文件，产出约3~5TB，已经持续稳定运营 8 个月。

2021 年 6 月，正式对外开源。

取决于代码逻辑及本地机器配置，`yadcc`可以利用几百乃至1000+核同时编译（内部而言我们使用512并发编译），大大加快构建速度。

具体简介及技术细节可以参考[我们的技术文档](yadcc/doc)。

## 系统要求

- Linux 3.10 及以上内核，暂不支持其他操作系统；
- x86-64 处理器；
- 编译`yadcc`需要GCC 8 及以上版本的编译器，基于`yadcc`进行分布式编译时可以支持其他更低版本编译器。

## 基本原理

和[`ccache`](https://ccache.dev)、[`distcc`](https://github.com/distcc/distcc)、[`icecc`](https://github.com/icecc/icecream)等工具类似；

- 我们的客户端伪装成编译器（通常是通过`ln -sf yadcc g++`创建的符号链接）
- 通过将我们的客户端伪装的编译器加入`PATH`头部，这样构建系统就会实际执行`yadcc`来编译
- `yadcc`会按照命令行对源代码进行预处理，得到一个自包含的的预处理结果
- 以预处理结果、编译器签名、命令行参数等为哈希，查询缓存，如果命中，直接返回结果
- 如果不命中，就请求调度器获取一个编译节点，分发过去做编译
- 等待直到从编译集群中得到编译结果，并更新缓存

由于预处理时间通常远小于编译时间，因此这样可以降低单个文件的本地开销。同时，由于等待编译结果时本地无需进行操作，因此可以增大本地的编译并发度（如8核机器通常可以`make -j100`），以此实现更高的吞吐。

需要注意的是，分布式编译通常只能提高吞吐，但是**不能降低单个文件的编译耗时**（假设不命中缓存）。因此，对于无法并发编译的工程，除非命中缓存，否则分布式编译通常不能加快编译，反而可能有负面效果。

## 设计特点

我们的系统由调度器、缓存服务器、守护进程及客户端组成：

- 对上层的构建系统（Make、CMake，Blade、Bazel 等）透明，方便适配各种构建系统。
- 调度器全局共享，所有请求均由调度节点统一分配。这样，**低负载时可允许客户端尽可能提交更多的任务，集群满载时可阻塞新请求避免过载**。
- 中心的调度节点也避免了需要客户机感知编译集群的列表的需要，降低运维成本。
- 编译机向调度器定期心跳，这样我们**不需要预先在调度器处配置编译机列表**，降低运维成本。
- **分布式缓存避免不必要的重复编译**。同时本地守护进程处会维护缓存的布隆过滤器，**避免无意义的缓存查询**引发不必要的网络延迟。
- 我们使用本地守护进程和外界通信，这避免了每个客户端均反复进行TCP启动等操作，降低开销。另外这也允许我们在守护进程处维护一定的状态，提供更多的优化可能。
- 客户端会和本地守护进程通信，**控制本地任务并发度**避免本地过载，这对服务器失效回落到本地编译非常重要。
- 我们通过编译器哈希区分版本，这**允许我们的集群中存在多个不同版本的编译器**。

同时，我们做了多层重试，确保不会因为网络抖动、编译机异常离线等工业场景常见的问题导致的不必要的失败。

## 开始使用

`Yadcc`自带了必要的第三方库，因此通常不需要额外安装依赖。

需要注意的是，`yadcc`通过[git-submodule](https://linux.die.net/man/1/git-submodule)引用[`flare`](https://github.com/Tencent/flare)，因此编译之前需要执行`git submodule update`拉取`flare`。另外由于flare代码仓库需要git-lfs支持，因此您还需要安装git-lfs。具体可以参考[`flare`的相关说明](https://github.com/Tencent/flare)：

```bash
git clone https://github.com/Tencent/yadcc --recurse-submodules
```

或

```bash
git clone https://github.com/Tencent/yadcc
cd yadcc
git submodule init
git submodule update .
```

### 编译`yadcc`

可以使用如下命令编译`yadcc`：

```bash
./blade build yadcc/...
```

### 搭建环境及使用

搭建环境及使用方式可以参考[详细文档](yadcc/doc/README.md)。

## 效果

我们搭建了一个 1000 多核的测试机群，在一些大型 C++ 项目上实测了效果。

LLVM 项目：

- 源代码：llvm-project-11.0.0.tar.xz。
- 机型：8C16G。
- 编译器：GCC8.2.0。

在我们的测试环境中共计 6124 个编译目标，结果如下：

- 本地8并发编译：47分51秒
- 分布式256并发：3分11秒

对于我们内部的一组更大的实际产品项目代码上：

- 16C 开发机本地 8 并发：2时18分17秒
- ccache+distcc, -j144：44分23秒
- 76C高性能开发机 -j80：25分18秒
- yadcc：9分25秒

详情参见[性能测试对比](yadcc/doc/benchmark.md)。

总体而言，`yadcc`应当有相当明显的性能优势。

## 相关项目

对于大型 C++ 项目，构建速度一直是个比较的问题，因此目前也已经有了一些相关的项目：

- https://ccache.dev/ 本地编译缓存
- https://github.com/distcc/distcc 远程编译，支持搭配 ccache 本地缓存
- https://github.com/icecc/icecream SUSE 开发的远程编译系统，fork 的 distcc，增加了调度器
- https://github.com/mozilla/sccache Mizilla 开发的分布式共享编译缓存
- https://github.com/alibaba/xcache Alibaba 开发的分布式共享编译缓存
- https://docs.bazel.build/versions/master/remote-caching.html Google Bazel 远程缓存协议，有多个实现，只支持 Bazel 或支持该协议的构建系统
- https://bazel.build/remote-execution-services.html Google Bazel 远程执行协议，有多个实现，只支持 Bazel 或支持该协议的构建系统

比较而言，Bazel 相关的两个协议是非跨构建系统的；distcc 缺乏统一的调度，其他几个只是编译缓存；icecream 是和 yadcc 最接近的，也是我们一开始试用的目标，不过在并行数百个任务时，表现未达到预期。

我们才开发了 yadcc，除了提供更好的性能和可靠性外，还额外增加了一些其他功能。

Yadcc 是我们目前已知的内置缓存机制的通用分布式编译系统。
