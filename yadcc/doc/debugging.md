# 调试

我们提供了一定的用于简化调试的能力。

## 客户端

客户端可以通过环境变量`YADCC_LOG_LEVEL`配置日志级别，默认为`2`（`INFO`）。可选项分别为`0` ~ `5`，分别对应`DEBUG` / `TRACE` / `INFO` / `WARN` / `ERROR`。我们会输出大于等于`YADCC_LOG_LEVEL`级别的日志。

另外作为一项针对延迟的优化，小文件`yadcc`可能会本地编译，可以通过`YADCC_COMPILE_ON_CLOUD_SIZE_THRESHOLD = 0`屏蔽这一行为。

通常可以通过这样的命令来测试基本流程（`-c`参数为必须，否则`yadcc`会因为这个命令不（止）是编译而直接本地执行。）：`YADCC_LOG_LEVEL=0 YADCC_COMPILE_ON_CLOUD_SIZE_THRESHOLD=0 /path/to/yadcc g++ ./test.cc -c`。

样例输出：

```text
[2020-01-01 00:00:00.000000] [TRACE] [yadcc/client/yadcc.cc:256] Started
[2020-01-01 00:00:00.000000] [DEBUG] [yadcc/client/utility.cc:76] Looking up for [g++] in [/usr/local/tools/gdb/bin].
[2020-01-01 00:00:00.000000] [DEBUG] [yadcc/client/utility.cc:76] Looking up for [g++] in [/usr/local/tools/gcc/bin].
[2020-01-01 00:00:00.000000] [TRACE] [yadcc/client/utility.cc:81] Found [g++] at [/usr/local/tools/gcc/bin].
[2020-01-01 00:00:00.000000] [TRACE] [yadcc/client/yadcc.cc:138] Using compiler: /usr/local/tools/gcc/bin/g++
...
```

## 守护进程

我们通过`flare::ExposedVar`在`/inspect/vars/yadcc`对外暴露了一些信息，这包括但不限于：

- 本地的编译器及其哈希值（集群中需要有哈希相同的编译器才能执行任务）。
- 当前任务数
- ...

可以通过命令行方式来查阅相关信息（必须使用`localhost`）：`curl http://localhost:8334/inspect/vars/yadcc`。

样例输出：

```jsonc
{
   "compilers" : {  // 本地提交任务用到过的编译器列表。
      "/usr/local/tools-opt/gcc-10/bin/g++" : {
         "digest" : "(redacted)",
         "mtime" : 1596119444,
         "size" : 1174552
      },
      "/usr/local/tools/gcc/bin/g++" : {
         "digest" : "(redacted)",
         "mtime" : 1534392565,
         "size" : 1128976
      },
      "/usr/local/tools/gcc/bin/gcc" : {
         "digest" : "(redacted)",
         "mtime" : 1534392569,
         "size" : 1124880
      }
   },
   "distributed_task_dispatcher" : null,  // 对于客户机，列出了提交到云端执行的任务。
   "execution_engine" : {  // 对于编译机而言列出了正在执行的网络上的任务。
      "jobs" : {
         "alive_tasks" : 0,
         "max_tasks" : 30,
         "running_tasks" : 0,
         "tasks_run_ever" : 0
      }
   },
   "local_task_mgr" : {  // 本地任务并发度控制相关信息。
      "heavyweight_waiters" : 0,
      "lightweight_task_overprovisioning" : 38,
      "lightweight_waiters" : 0,
      "max_tasks" : 38,
      "running_tasks" : 0
   }
}
```

## 调度器、缓存服务器

原理上来讲，这两个服务也是通过`flare::ExposedVar`对外暴露调试信息。

但是由于`/inspect/gflags`还提供了获取系统中[GFlags](https://github.com/gflags/gflags)的能力，而`token`又是通过GFlags配置的。如果不加上访问控制，会导致`token`形同虚设。因此，默认而言，这两个服务的`/inspect`接口均会返回HTTP 403。

可以通过如下参数启用`/inspect/var/...`：

- `--inspect_credential`：指定访问`/inspect/...`需要的身份信息，留空时完全禁用`/inspect/...`接口。

  这儿我们使用[HTTP Basic Authentication](https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication#Basic_authentication_scheme)来认证。

  具体而言，这一配置应当形如`username:password`的方式来配置，如`--inspect_credential=username:password`。

之后可以命令行方式查询信息（调度器默认端口`8336`、缓存服务器默认端口`8337`）：`curl -s http://username:password@server-ip:server-port/inspect/vars/yadcc`。

调度器的样例输出：

```jsonc
{
   "task_dispatcher" : {
      "capacity" : 128,  // 集群总核数（可用于编译的，非所有机器总核数）。
      "capacity_available" : 128,  // 当前可用核数。
      "capacity_unavailable" : 0,  // 当前不可用核数（高负载、低内存等）。
      "running_tasks" : 0,  // 运行中的任务数。
      "servants" : [  // 这儿列出来了集群中的机器信息。
         {
            "capacity_available" : 0,
            "current_load" : 1,
            "discovered_at" : "2020-01-01 00:00:00",
            "environments" : [  // 这台机器上所有的编译器版本。
               "(redacted)",
               ...
            ],
            "ever_assigned_tasks" : 0,
            "expires_at" : "2020-01-01 00:00:00",
            "location" : "192.0.2.1:8335",
            "not_accepting_task_reason" : "NOT_ACCEPTING_TASK_REASON_POOR_MACHINE",  // 存在时标明这台机器不接受编译任务的原因。
            "num_processors" : 8,
            "priority" : "SERVANT_PRIORITY_USER",
            "running_tasks" : 0,
            "version" : 6
         },
         {
            "capacity_available" : 76,
            "current_load" : 1,
            "discovered_at" : "2020-01-01 00:00:00",
            "environments" : [
               "(redacted)",
               ...
            ],
            "ever_assigned_tasks" : 0,
            "expires_at" : "2020-01-01 00:00:00",
            "location" : "192.0.2.2:8335",
            "max_tasks" : 76,  // 机器所能接受的最大任务数
            "num_processors" : 80,
            "priority" : "SERVANT_PRIORITY_DEDICATED",
            "running_tasks" : 0,
            "version" : 6
         },
         ...
      ],
      "servants_up" : 5  // 集群中机器总数
   }
}

```

缓存服务器样例输出：

```jsonc
{
   "cache" : {
      "l1" : {  // 1级缓存（内存）。
         "actual_entries" : 0,
         "actual_size_in_bytes" : 0,
         "hits" : 0,
         "misses" : 0,
         "phantom_entries" : 0,
         "phantom_size_in_bytes" : 0
      },
      "l2" : {  // 2级缓存（存储介质取决于`--cache_engine`配置）。
         "partitions" : {
            "./cache" : {
               "capacity_in_bytes" : 34359738368,  // 磁盘缓存的容量。
               "entries" : 0,
               "hits" : 0,
               "used_in_bytes" : 0
            },
            "total_entries" : 0
         },
         "statistics" : {
            "fills" : 0,
            "hits" : 0,
            "misses" : 0,  // 缓存未命中数。由于布隆过滤器的存在（见`cache.md`中描述），因此通常这个值很低。
            "overwrites" : 0  // 覆盖缓存的次数，一般是由于多机同时编译相同的代码（均在对方填充缓存之前即开始编译）导致。
         }
      }
   }
}
```

## FAQ

- Q：编译无进度，客户端日志在`[yadcc/client/compilation_saas.cc:313] Compilation task [...] is successfully submitted.`之后无进度。

  A：可以观察守护进程的调试信息，是否有任务一直在等待。如果是，可以对比守护进程中编译器哈希及调度器处集群中存在的编译器哈希，检查是否有`capacity_available`不为0的机器含有所需的编译器。

- Q：集群中所有机器的`capacity_available`都为0。

  A：默认情况下我们对机器是否提供服务、提供的服务能力都相对保守。只有高配（24逻辑核及以上）的非容器环境，默认配置下才会主动提供服务。可以通过`--max_remote_tasks`参数来覆盖这一行为，明确要求对外提供服务。
