hash部分的源码主要分布在assoc.h/c（hash查找、插入、删除，rehash策略）、hash.h/c（尽提供几个基本的hash函数）

解决冲突的方法，memcached中采用了链地址法（或拉链法）

primary_hashtable和old_hashtable，当hashtable的填装因子（memcached中硬编码为 3/2，无法确定如何定值的），assoc_maintenance_thread线程会将old_hashtable中的items以hash_bulk_move个buckets为单位，逐步移到primary_hashtable中。

        对外接口：

        // 完成primary_hashtable的初始化
        void assoc_init(void);   

        // 根据key和nkey查找对应的item
        item *assoc_find(const char *key, const size_t nkey); 

        // 将item插入hashtable中
        int assoc_insert(item *item); 

        // 从hashtable中删除key对应的item
        void assoc_delete(const char *key, const size_t nkey);

        // 下面两个函数分别为启动和结束hashtable维护的线程，如果不需要这个功能，可以不调用，就是会浪费primary_hashtable指针数组占用的内存资源
        int start_assoc_maintenance_thread(void);
        void stop_assoc_maintenance_thread(void);

        void do_assoc_move_next_bucket(void); // 函数没有实现



几个用到的变量：

       static pthread_cond_t maintenance_cond; // 同步insert和hashtable维护线程的条件变量

       static unsigned int hashpower = 16;          
       #define hashsize(n) ((ub4)1<<(n))              //hashtable初始化大小设置为2^16
       #define hashmask(n) (hashsize(n)-1)         // 初始hashmask=0x1111 1111 1111 1111，用来将哈希函数计算的结果映射到hashsize域内，hashmask二进制值永远是hashpower位1的串

      static unsigned int hash_items = 0;            // hashtable中存在的item数量

      static bool expanding = false;                     // 是否正在扩展的flag

      static unsigned int expand_bucket = 0;      // 当前扩展的位置（old_hashtable中的索引）


comment from:

memcached源码分析之线程池机制（一） http://www.cnblogs.com/moonlove/archive/2012/07/10/2584428.html

memcached源码分析之线程池机制（二） http://www.cnblogs.com/moonlove/archive/2012/07/10/2584833.html

memcached源码学习-hashtable  http://blog.csdn.net/tankles/article/details/7032756

TODO 专栏>Memcached源码分析 http://blog.csdn.net/column/details/lc-memcached.html?page=1

to view:
从Memcached看锁竞争对服务器性能的巨大影响 http://my.oschina.net/wzwitblog/blog/163705

 原文见于http://shiningray.cn/scaling-memcached-at-facebook.html，不过此文对翻译进行了一些自认为的修改和内容的归纳总结。这里有英文原文：http://guojuanjun.blog.51cto.com/277646/735854

http://www.facebook.com/notes/facebook-engineering/scaling-memcached-at-facebook/39391378919 

Memcached是一个高性能、分布式的内存对象缓存系统。Facebook利用Memcached来减轻数据库的负担，可能是世界上最大的Memcached用户了（使用了超过800台服务器，提供超过28TB的内存来服务于用户）。

Memcached确实很快了，但是Facebook还是对Memcached进行了4处大的修改来进一步提升Memcached的性能。浏览下面的4个修改，不难发现其中2个都和“严重的锁竞争”有关。修改1主要是节省了大量内存，所以减少“锁竞争”对性能提升的贡献估计能达到70%左右，由此可见“锁竞争”的可怕！！！。修改后的代码在这里：https://github.com/fbmarc/facebook-memcached-old（根据文章中的链接找到的）

当然Facebook对Memcached的优化也不能说明Memcached很差，正是因为它足够优秀，Facebook这种牛人云集的公司才会用它。而且Memcached自身的文档也提到了在多线程情况下性能会因为锁竞争下降，见最下面一段。
1. 内存优化：

原状：memcached为“每个TCP链接”使用单独的缓存（a per-connection buffer）进行数据的读写。
问题：当达到几十万链接的时候，这些累计起来达好几个G——这些内存其实可以更好地用于存储用户数据。

方案：实现了一个针对TCP和UDP套接字的“每线程共享”的链接缓存池（a per-thread shared connection buffer pool for TCP and UDP sockets）。
效果：这个改变使每个服务器可以收回几个G的内存。

这个让我想起了云风大哥的“Ring Buffer 的应用”这篇博客。http://blog.codingnow.com/2012/02/ring_buffer.html
2. 网络流量优化（及由此引起的UDP套接字锁竞争）
优化目的：使用UDP代替TCP，让get（获取）操作能降低网络流量、让multi-get（同时并行地获取几百个键值）能实现应用程序级别的流量控制。

新问题：我们发现Linux上到了一定负载之后，UDP的性能下降地很厉害。
新问题原因：当从多个线程通过单个套接字传递数据时，在UDP套接字锁上产生的大量锁竞争（considerable lock contention）导致的。

新问题方案：要通过分离锁来修复代码核心不太容易。所以，我们使用了分离的UDP套接字来“传递回复”（每个线程用一个回复套接字）。
新问题解决效果：这样改动之后，我们就可以部署UDP同时后端性能不打折。
3. 网络IO优化：

问题：(1)Linux中的问题是到了一定负载后，某个核心可能因进行网络软终端处理会饱和而限制了网络IO。(2)特定的网卡有特别高的中断频率。

问题(1)的原因：在Linux中，网络中断只会总是传递给某个核心，因此所有接收到的软中断网络处理都发生在该核心上。

解决：我们通过引入网络接口的“投机”轮询（“opportunistic” polling of the network interfaces）解决了这两个问题。在该模型中，我们综合了中断驱动和轮询驱动的网络IO。一旦进入网络驱动（通常是传输一个数据包时）以及在进程调度器的空闲循环的时候，对网络接口进行轮询。另外，我们还是用到了中断（来控制延迟），不过用到的网络中断数量相比大大减少了（一般通过大幅度提升中断联结阈值interrupt coalescing thresholds）。
效果：由于我们在每个核心（core）上进行网络传输，同时由于在调度器的空闲循环中对网络IO进行轮询，我们将网络处理均匀地分散到每个核心（core）上。
4. 8核机器优化：
(1) stat收集

问题：memcached的stat收集依赖于一个全局锁。这在4核上已经很令人讨厌了，在8核上，这个锁可以占用20-30%的CPU使用率。
方案：我们通过将stat收集移入每个线程，并且需要的时候将结果聚合起来。
(2) UDP线程不能scale

问题：随着传输UDP数据包的线程数量的增加，性能却在降低。

原因：保护每个网络设备的传送队列的锁上发现了严重的争用（significant contention）。传输时将数据包入队，然后设备驱动进行出队操作。该队列由Linux的“netdevice”层来管理，它位于IP和设备驱动之间。每次只能有一个数据包加入或移出队列，这造成了严重的争用（causing significant contention）。

方案：一位工程师修改了出队算法以达到传输时候的批量出队，去掉了队列锁（drop the queue lock）。这样就可以批量传送数据包了。
效果：将锁请求（the lock acquisition）的开销平摊到了许多个数据包上，显著地减少了锁争用（reduces lock contention significantly），这样我们就能在8核系统上将memcached伸展至8线程。

最终优化效果：
“做了这些修改之后，我们可以将Memcached提升到每秒处理20万个UDP请求，平均延迟降低为173微秒。可以达到的总吞吐量为30万UDP请求/s，不过在这个请求速度上的延迟太高，因此在我们的系统中用处不大。对于普通版本的Linux和Memcached上的50,000 UDP请求/s而言，这是个了不起的提升。”

20万/s相对于5万，这个性能提升太吓人了！！！

不过Memcached源码doc目录下的thread.txt也有这样的一句话：

Due to memcached's nonblocking architecture, there is no real advantage to using more threads than the number of CPUs on the machine; doing so will increase lock contention and is likely to degrade performance.

然后还提到了当前的锁的粒度比较粗，对于在大规模并行的机器上运行还是有很多优化空间的。也提到了所有客户端都共享一个UDP套接字。

所以使用开源软件之前还是最好先阅读下它的相关文档，ChangeNodes，TODO等。比如这里就有一个教训：http://blog.codingnow.com/2009/06/tcc_bug.html  花了很多时间确认了Tcc的一个bug，但是后来才发现，这个bug早就在TODO文件里面了。 
