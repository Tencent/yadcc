# HTTP接口

为了保证我们的客户端启动的足够快，我们故意避免了使用Flare来实现我们的客户端。

这儿主要的问题在于，Flare启动过程，为了进行必要的初始化，会引入数百毫秒的延迟。

通常而言，数百毫秒的启动延迟对于长期运行的服务并没有太大影响。但是对于我们的客户端来说，由于它对于每个编译任务会启动、退出一次，这个延迟是不可接受的。

因此，我们通过[libcurl](https://curl.haxx.se/libcurl/)实现了我们的客户端，并通过HTTP协议和我们本地的编译守护进程进行交互。

这篇文档描述了我们的客户端和本地守护进程之间的接口。

除非额外说明，下述接口均需要使用`POST`方法请求。

注：确实后面所描述的部分接口可以通过Protocol Buffers来实现（为`http+gdt-json`协议），但是为了保证接口看起来一致，我们全部选择使用普通的HTTP协议实现。此外，将部分接口实现为Protocol Buffers并没有太多的技术收益。

## 查询版本

为了能够支持自动升级，我们提供了这一方法用于外界（如调用脚本）查询正在运行的守护进程的版本。

这一接口使用`GET`方法请求。

### 请求

- URI：`/local/get_version`

### 响应

- 头：

  - `Content-Type`：`application/json`

- 正文：参考`messages.proto`中`GetVersionResponse`定义。HTTP正文是这一消息的JSON形式表达。

### 响应

## 获取本地任务配额

无论是预处理，或者是对于较小的TU直接本地编译（避免网络延迟）、云端失败本地重试，我们的客户端在运行编译器之前都会向本地的守护进程请求一个任务配额。这有助于控制本地的并发任务数避免将机器压跨。

### 请求

- URI：`/local/acquire_quota`

- 头：

  - `Content-Type`：`application/json`

- 正文：参考`messages.proto`中的`AcquireQuotaRequest`定义。HTTP正文是这一消息的JSON形式表达。

### 响应

同请求，但是响应中我们使用`AcquireQuotaResponse`消息。

## 释放本地任务配额

这个接口用于释放之前通过`/local/acquire_quota`获取的任务配额。

出于容灾考虑，我们的守护进程实际上并不强制客户端对每次`/local/acquire_quota`均对应的调用这一接口。

守护进程会定期的检查之前获取过任务配额的进程的存活性（通过之前请求配额时提交的`requestor_pid`），并在客户端退出时自动释放相应的配额。

主动调用这一接口可以改善释放配额的时效性。

### 请求及响应

除了如下区别，同“获取本地任务配额”相同：

- URI使用`/local/release_quota`

- 响应中使用`ReleaseQuotaRequest`、`ReleaseQuotaResponse`消息。

## 提交编译任务

客户端可以通过这个接口将编译任务提交给云端。

### 请求

- URI：`/local/submit_task`

- 头：（只列出需要客户端明确指定的字段）

  - `Content-Encoding`：按照HTTP中的可选项指定压缩算法。目前而言我们只支持[`zstd`](https://github.com/facebook/zstd)，硬编码为`application/zstd`。（另外`zstd`目前尚是一个扩展。）

  - `X-Requestor-Process-Id`：客户端自己的PID。我们的守护进程会定期检查这个进程存活性。如果用户在编译过程中终止了编译行为，我们的守护进程可以根据这一点提前取消提交给云端的任务。

  - `X-Compiler-Path`：编译器路径。

  - `X-Compiler-Modification-Time`：编译器`mtime`的时间戳。64位整数。

  - `X-Compiler-Size`：编译器大小（字节）。

  - `X-Compiler-Digest`：编译器的BLAKE3哈希。为了区分不同编译器的版本，在云端我们只会用哈希匹配的编译器来编译。

    这一字段并非始终必要。

    由于计算哈希的固有开销，我们在本地守护进程会根据`X-Compiler-Path`、`X-Compiler-Modification-Time`、`X-Compiler-Size`作为key来缓存`X-Compiler-Digest`。

    通常而言，客户端请求这一接口时不会提供`X-Compiler-Digest`（期望守护进程中已经有了`X-Compiler-Digest`的缓存）。如果服务端返回HTTP 400，则会自行计算编译器哈希，并重新提交任务。

    *我们不能依赖于守护进程去计算编译器哈希。由于守护进程可能运行在不同账户中，因此可能没有权限访问客户端所使用的编译器。*

  - `X-Source-Path`：源代码的相对路径。严格来说这个参数并不是必须的，我们用这个参数来做调试。

  - `X-Compiler-Invocation-Arguments`：在云端执行编译器时传入的参数。需要注意的是，请求方需要保证参数列表中包含参数`-`，这样编译器才会从`stdin`处读取（预处理后的）源代码。

    **XXX：允许任务提交方自行制定参数实际上存在安全风险，比如如果提交方将`/etc/passwd`等文件作为输入或输出。**

  - `X-Cache-Control`：客户端通过这个参数来指定是否允许对这个任务启用编译缓存。

    - 0：禁用缓存
    - 1：启用缓存
    - 2：不使用现存的缓存但用编译后的结果填充缓存

  - `X-Source-Digest`：源代码的BLAKE3哈希（压缩前）。如果`X-Cache-Control`为0，则这一参数无意义。

- 正文：预处理后的源代码（压缩后）。

### 响应

JSON响应，格式见`messages.proto`中的`SubmitTaskResponse`。

## 等待编译任务完成并获取输出

这个接口通过长轮询的方式提供。

客户端应当轮询这个接口直到编译结束（无论成功或失败）。编译结束之后，守护进程会通过这个接口将编译结果返回给客户端，并销毁这个编译任务的相关上下文。

### 请求

- URI：`/local/wait_for_task`

- 头：

  - `Content-Type`：`application/json`

- 正文：`messages.proto`中`WaitForTaskRequest`消息的JSON表示。

### 响应

- 状态码：

  - 200：编译结束，编译结果通过正文返回

  - 404：给定的任务ID无法识别

  - 503：返回时编译任务仍在进行中

- 正文：

  正文中分为两段，使用类似于`chunked`编码方式进行编码，但是没有最终的“空chunk”。（如：`2\r\n{}\r\nA\r\n0123456789\r\n`）：

  - 第一段是`WaitForTaskResponse`消息的JSON表达。

  - 第二段是压缩后的编译结果。

## 要求守护进程退出

在这个方法被调用时，守护进程会退出。

通常客户端会在发现守护进程版本过低时通过这个方法要求其退出并启动更新版本的守护进程。

### 请求

- URI：`/local/ask_to_leave`

注：尽管严格来讲并非必须，受限于Flare的实现，请求中必须指定`Content-Length`字段（可以硬编码为0）。

### 响应

（无）
