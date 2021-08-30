# 客户端

针对不同的编程语言，我们提供了对应的客户端：

- [C++](client/cxx.md)

## 配置

关于针对不同编程语言分别提供的配置项，可以参考对应的客户端的文档。

此处主要列出适用于所有客户端的配置参数。

*部分工具链（如[Bazel](https://bazel.build/designs/2016/06/21/environment.html#current-shortcomings)）会在调用编译器之前“重置”环境变量。此时需要用户自行采取工具链特有的方式传递如下配置项给`yadcc`。*

- `YADCC_WARN_ON_WAIT_LONGER_THAN`：整数类型。如果在等待了这么多秒之后本地守护进程依然没有授予wrapper开始执行命令的权力，会输出一条warning级别日志。

- `YADCC_CACHE_CONTROL`：控制缓存行为，可选参数：

  - `0`：禁用缓存
  - `1`：启用缓存（默认值）
  - `2`：不使用缓存，但用编译结果更新已有缓存。这一选项通常用于调试目的。

- `YADCC_LOG_LEVEL`：日志级别。0~5对应`DEBUG` / `TRACE` / `INFO` / `WARN` / `ERROR`。

- `YADCC_DAEMON_PORT`：本地守护进程的监听端口，默认`8334`。
