# 性能对比

此处列出了一些对比数据。所有测试均不命中缓存。

## 相对于本地编译的对比

8C虚拟机，256并发度，使用[`llvm-project-11.0.0.tar.xz`](https://github.com/llvm/llvm-project/releases/tag/llvmorg-11.1.0)测试。

可能取决于机器环境，**不同机器上`cmake3`生成的目标数不一定一样。此处我们的环境中共计6124个编译目标。**

## 本地编译

命令行：`time ninja`

```text
[6124/6124] Linking CXX executable bin/opt

real	47m51.414s
user	356m17.391s
sys	23m25.461s
```

## 分布式编译

### 分布式256并发、本地4并发

`YADCC_CACHE_CONTROL=2`表示不读取缓存，但是执行缓存相关逻辑并写入缓存。主要用于调试目的。

命令行：`time YADCC_CACHE_CONTROL=2 ninja -j256`

```text
[6124/6124] Linking CXX executable bin/clang-check

real	3m11.292s
user	16m48.304s
sys	4m24.946s
```

## 相对于`ccache` + `distcc`的对比

我们基于内部的蓝盾平台提供的`ccache` + `distcc`进行对比，16C虚拟机中以144并发度编译约18k个目标：

*本地编译采用16并发时编译过程中OOM导致编译器被kill，故本地编译采用8并发。*

- 本地8并发：2时18分17秒。
- ccache+distcc：44分23秒。
- yadcc：9分25秒。

需要注意的是，我们内部提供的`ccache` + `distcc`会对本地并发度进行限制（同`yadcc`一样，避免本地过载）。且我们观察到了部分失败重试，这些重试一定程度上阻塞了后续分布式编译任务的生成，因此跑出来的结果可能较于理想条件下更差。但是整体而言，`yadcc`相对于`ccache` + `distcc`依然应当有较为明显的优势。
