# 使用说明

`yadcc`可以通过`./blade build yadcc/...`编译。

编译完成后，至少需要启动如下服务：

- 需要一台用于调度任务的机器，并运行`yadcc/scheduler/yadcc-scheduler`。具体使用可以参考[调度器文档](scheduler.md)。如：`./yadcc-scheduler --acceptable_user_tokens=some_fancy_token --acceptable_servant_tokens=some_fancy_token`。

  *尽管生产环境中不推荐，测试时调度机可以和编译机、用户机位于同一台机器。*

为了加速编译（用户**之间**互相共享编译结果），还可以启动如下服务：

- 缓存服务器上运行`yadcc/cache/yadcc-cache`。具体使用可以参考[缓存服务器文档](cache.md)。如：`./yadcc-cache --acceptable_user_tokens=some_fancy_token --acceptable_servant_tokens=some_fancy_token`。

  *尽管生产环境中不推荐，测试时缓存服务器可以和调度器、编译机、用户机位于同一台机器。*

  缓存服务器并非必须，但是对于基于大仓库的开发模式而言，往往不同用户之间可以共享编译缓存，此时缓存服务器会有一定的收益。

另外，在搭建过程中如果遇到一些问题，也可以参考我们的[调试文档](debugging.md)来尝试定位。

## 使用

`yadcc`并不区分编译机及用户机。默认情况下，用户为了提交任务至编译集群，即会自动贡献一部分CPU至编译集群供其他用户使用。

### 启动守护进程

每台客户机上需要启动`yadcc`的守护进程（位于`yadcc/daemon/yadcc-daemon`，不需要`root`权限），具体使用可参考[守护进程文档](daemon.md)。如：`./yadcc-daemon --scheduler_uri=flare://ip-port-of-scheduler --cache_server_uri=flare://ip-port-of-cache-server --token=some_fancy_token`。

需要注意的是，**低配机器（CPU核心数小于等于`--poor_machine_threshold_processors`，默认16）默认不接受任务**，其余机器默认贡献40%的CPU至集群。

对于专有编译机，可以通过增加`--servant_priority=dedicated`，这样这台机器始终会将95%的CPU贡献至编译集群。

如果不使用缓存服务器，则`--cache_server_uri`参数不需要。

### 整合构建系统

`yadcc`可以和类似于[`distcc`](https://github.com/distcc) / [`icecream`](https://github.com/icecc/icecream)等分布式编译系统相同的方式，既可以通过软链接到`g++` / `gcc`实现整合，也可以通过（对于某些构建系统）覆盖`CXX` / `CC` / `LD`环境变量整合。

之后用户应当设置一个较大编译并发度（因为本地很多时候是在等待网络上的任务完成，不占用CPU，所以并发度太小可能导致本地空等）。

#### 通过符号连接（blade、bazel、Makefile、...）

1. 创建目录`~/.yadcc/bin`、`~/.yadcc/symlinks`。
2. 复制`build64_release/yadcc/client/cxx/yadcc-cxx`至`~/.yadcc/bin`。
3. 创建软链接`~/.yadcc/symlinks/{c++,g++,gcc}`至`~/.yadcc/bin/yadcc-cxx`。
4. 将`~/.yadcc/symlinks`加入`PATH`的头部。

执行完毕上述操作之后`which gcc`应当输出类似于`~/.yadcc/symlinks/gcc`的结果。

**请注意，创建软链并通过软链`~/.yadcc/symlinks/g++`执行并不等同于直接执行`~/.yadcc/bin/yadcc`**。这两种方式下`yadcc`收到的`argv[0]`不同。当`yadcc`检测到`argv[0]`是`gcc`或`g++`时，有特殊逻辑来使得`yadcc`的行为“看起来像”一个真实的GCC编译器。这也是实现“drop-in replacement”的基础。

部分构建系统在上述配置之后可能需要清空缓存（如[`bazel`](https://bazel.build)需要`bazel clean && rm -rf ~/.cache`），之后再次编译应当`yadcc`即可生效：如`make -j500`。

#### 通过环境变量（blade、bazel、...）

部分构建系统（如`blade` / `cmake`等）在监测到用户覆盖了`CXX` / `CC` / `LD`变量时，会使用用户指定的命令行来编译，如：

```bash
CXX='/path/to/yadcc g++' CC='/path/to/yadcc gcc' LD='/path/to/yadcc g++' ./blade build //path/to:target -j500
```

#### CMake

特别的，[`cmake`](https://cmake.org)可以通过如下方式整合：

- 创建合适的符号链接，如`yadcc/symlinks/g++`、`yadcc/symlinks/gcc`。
- 手动指定`CXX` / `CC`来运行`cmake`：`CXX=yadcc/symlinks/g++ CC=yadcc/symlinks/gcc cmake .`。

`cmake`生成的编译脚本会记录编译器路径，后续无需额外操作（如修改`PATH`等均不需要），正常编译即可。

## 其他

这一节列举了一些我们在调试过程中遇到的问题及经验。

### 构建系统的选择

根据我们的观察，[`ninja`](https://ninja-build.org/)在高并发时表现出色。

在上文的`llvm-project`对比测试中，同样由`cmake`生成编译脚本（`-G Ninja`、`-G "Unix Makefiles"`）的情况下，`ninja`表现明显优于`Makefile`。（可能这也是"Most llvm developers use Ninja."的原因。）

因此对于**编译性能要求很高的场景，我们建议配合`ninja`系构建系统使用**。

*`blade`基于`ninja`实现（早期版本见后文），`cmake`也可以生成`ninja`的编译脚本。*

**`bazel`在高（数百）并发下表现相对其他构建系统明显较差**，见后文。

### Blade（早期版本）

早期版本（2018年之前）的`blade`基于[`SCons`](https://scons.org)实现。并发稍高（20并发+）即会有明显性能问题。

新版本`blade`已经切换至[`ninja`](https://ninja-build.org)引擎，我们观察中在1000并发以内应当不存在瓶颈。

可以通过观察`blade`启动时输出确定使用的引擎。如果启动时有`SCons`相关字样则是`SCons`版本。

**我们强烈建议使用`SCons`版本的用户升级至`ninja`引擎的`blade`**。

### Bazel

*`bazel`默认会限制并发度到其估计的机器性能上限，实际使用需要通过`--local_cpu_resources=9999999 --local_ram_resources=999999`等参数绕过这一限制。*

我们**已知（部分版本的）`bazel`在并发度过高下，`bazel`自身性能存在瓶颈**（如我们内部常用的`-j320`。事实上，根据我们观察，160并发`bazel`已经不能很好的支撑）。

这具体表现为机器空闲但不会启动更多编译任务，同时`bazel`自身CPU（400~500%）、内存（几G）占用很高。

这一问题目前只存在于`bazel`，`blade` / `cmake` / `ninja` / 手写`Makefile`等构建系统不存在此问题。

如果机器资源充足且对并发度有较高要求（几百并发），可以考虑使用其他构建系统构建。
