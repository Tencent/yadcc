# 缓存服务器

由于编译缓存通常较大（我们的负载中单个编译结果在[zstd](https://github.com/facebook/zstd)压缩之后平均约1M），使用[Redis](https://redis.io)这样的全内存数据库并不经济，因此我们决定自行设计一套缓存服务。考虑到需求可能随时会发生改变，我们也保留充分扩展缓存服务器的可能。

## 总体设计

参考缓存多层次结构，我们将缓存设计成了2层（简称L1和L2）。通常L1比L2缓存更快成本也更昂贵，L2缓存相对来说比较大并且更加可靠，可以认为是主缓存。

### L1缓存

L1缓存毫无疑问是基于内存，直觉上是缓存热点数据，并采用一定淘汰策略保持大小可控。算法是参照[ARC](https://www.usenix.org/legacy/events/fast03/tech/full_papers/megiddo/megiddo.pdf)实现，该算法会自适应的在LRU和LFU中进行折中，并不需要人工去调节参数。短期我们认为不会有明显更优的方案，所以采用默认实现足以，并不需要考虑扩展问题。

### L2缓存

为了便于系统今后方便扩展，适应更多存储方案，我们抽象了底层存储引擎实现。当我们需要其他存储方案时，可以快速实现一套底层存储方案，并通过修改配置选择对应的存储方案，并不需要修改核心逻辑。

可考虑缓存方案如下：
- NULL缓存（已支持。表示无L2缓存，完全依赖L1）
- 基于磁盘（已支持）
- 基于NoSQL（待支持）
- 基于分布式文件系统（待支持）

#### NULL缓存

如何使用该L2缓存选项，表示你并不需要L2缓存，那么所有的缓存将在内存进行。如果程序重启，将丢失所有的缓存记录。

#### 基于磁盘

考虑到编译缓存的如下一些特点：

- 编译缓存对空间占用敏感：NoSQL引擎通常使用SST+Compaction，这样的设计通常有较大的额外空间开销。
- 单个编译结果通常较大：将各个缓存项分别作为磁盘文件的设计引发的“大量小文件导致磁盘利用率低”的问题对我们的场景并不存在。
- 虽然系统管理的Page Cache不一定可以做到最优，但是可以预期的在一定程度上可以实现“分级存储”的效果。另外，以1M的平均大小来计算，做到100QPS就可以跑满1Gbps带宽，即便Page Cache完全不命中，实际场景中磁盘IO不一定会是性能瓶颈。

因此我们放弃了使用[LevelDB](https://github.com/google/leveldb)、[RocksDB](https://rocksdb.org)等NoSQL引擎，而选择直接基于磁盘文件保存缓存。

## 参数

缓存服务器有如下参数可以配置：

- `--acceptable_user_tokens`：逗号分隔的一系列`token`，用于标识客户端机器。调度器会校验请求中的`token`，如果不匹配于这一参数中的任何一个，则会拒绝请求。

- `--acceptable_servant_tokens`：同上，用于标识编译机。

- `--cache_engine`：配置L2缓存的存储方案，目前仅支持NULL和磁盘方案，对应选项：`null`，`disk`。示例：`--cache_engine=disk`。

### L1缓存

L1缓存有如下参数可以配置：

- `--max_in_memory_cache_size`: 配置l1缓存的大小。支持标准单位`G、M、K`，默认单位字节。示例：`--max_in_memory_cache_size=48G`。

### L2缓存

根据不同的L2缓存，可能需要配置不同的参数。

#### 基于磁盘

基于磁盘的L2有如下参数可以配置：

- `--disk_engine_cache_dirs`：一个或多个用于保存编译缓存的目录大小上限及路径。目录权重和路径之间以`,`分割。目录之间以`:`分割。如`--disk_engine_cache_dirs=10G,/path/to/disk1:20G,/path/to/disk2`。
    内部而言，我们对缓存的Key进行一致性哈希后决定使用哪一个目录，因此增加或删除目录不会显著的降低缓存命中率。（之后随着缓存内容的更新，命中率也会逐步回升。）
    如果机器配备了多个物理磁盘（或网络磁盘），这样可以实现类似于软磁盘阵列的效果，提升整体吞吐。

- `--disk_engine_action_on_misplaced_cache_entry`：`yadcc-cache`启动时会扫描`disk_engine_cache_dirs`中的文件，如果文件所在的目录和预期不符（如增加了新的目录至`disk_engine_cache_dirs`），`yadcc-cache`将会根据这儿指定的行为操作：

  - `delete`：删除相应缓存项；
  - `move`：将相应的缓存项移动到其所属的目录；
  - `ignore`：忽略。

## 缓存的布隆过滤器

如我们在基本原理(rationale.md)中所述，实际的生产场景中，编译缓存的命中率并不高。

另一方面，如果盲目的查询缓存，每次未命中的查询都会引入不必要的网络通信延迟。

因此，除了提供缓存读写能力之外，我们的缓存服务器还允许[守护进程](daemon.md)从这儿定期（增量或全量）获取一个对应于缓存本身的布隆过滤器用于预判掉大部分缓存不命中的场景。

*拉取布隆过滤器是一个对带宽开销较大的操作。同时，为了保证时效性，拉取的间隔不能太长。因此，我们提供了增量更新的能力同时兼顾带宽使用和时效性。但是另一方面，增量拉取无法移除可能已经过期的Key，只能用来补充新增加的Key，因此每隔一段（较长的）时间我们会拉取一次全量的布隆过滤器。*

内部而言，我们主要维护了如下状态：

- 近期新增的缓存Key：这个主要用于守护进程增量更新布隆过滤器的场景，可以获取过去一段时间新增的Key。
- 定期重建的全量布隆过滤器：出于控制缓存的空间开销考虑，我们实际上会淘汰老旧的缓存项，这使得单纯的“增加新增Key”无法反映实际的缓存状态（淘汰的key会被认为依然存活，增加假阳性比率）。因此，我们还会定期重建整个布隆过滤器，并在守护进程的布隆过滤器过于老旧时，直接返回全量布隆过滤器以将假阳性的比率控制在一个合理的范围内。

## 部署

我们的环境中采取和调度器同机部署，也可以根据实际情况使用专有机器部署（网络可达即可）。
