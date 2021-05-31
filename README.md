# Yadcc 分布式 C++ 编译器

`yadcc`是一套工业级C++分布式编译系统。

取决于代码逻辑及本地机器配置，可以利用几百乃至1000+核同时编译，大大加快构建速度。

*目前我们生产环境中使用320并发，这在16C及更高配置的机器上可以轻易达到。同时，我们使用内部`.proto`繁重的生产代码测试，在76C虚拟机上以`-j1100`编译，调度器处数据显示部分时段可以维持在950~1050并发。受制于机器条件我们没能够测试更高并发度下的表现。*

具体简介及技术细节可以参考[我们的技术文档](yadcc/doc)。

## 特点

- 集中式的编译任务调度，避免多用户场景下因任务量大导致编译集群过载
- 分布式编译缓存，支持用户间的编译结果复用
- 本地常驻守护进程，控制本地任务并发度避免本地过载
- 支持多种版本编译器共存

## 系统要求

- Linux 3.10 及以上内核，暂不支持其他操作系统
- x86-64 处理器
- 编译`yadcc`需要GCC 8 及以上版本的编译器，基于`yadcc`进行分布式编译时可以支持其他更低版本编译器

## 开始使用

`yadcc`自带了必要的第三方库，因此通常不需要额外安装依赖。

`yadcc`通过[git-submodule](https://linux.die.net/man/1/git-submodule)引用[`flare`](https://github.com/Tencent/flare)，因此编译之前需要执行`git submodule update`拉取`flare`：

*需要注意的是，flare代码仓库需要git-lfs支持，因此您还需要安装git-lfs。具体可以参考[`flare`的相关说明](https://github.com/Tencent/flare)*

```bash
git submodule init
git submodule update .
```

### 编译`yadcc`

可以使用如下命令编译`yadcc`：

```bash
./blade build yadcc/...
```

### 搭建环境及使用

搭建环境及使用方式可以参考[我们的文档](yadcc/doc/README.md)。

## 效果

效果可以参考[我们对比数据](yadcc/doc/benchmark.md)。

总体而言，相对于已有实现，`yadcc`应当有较为明显的性能优势。
