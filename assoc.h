/* associative array */

// hash部分的源码主要分布在assoc.h/c（hash查找、插入、删除，rehash策略）、hash.h/c（尽提供几个基本的hash函数）
// emcached中采用了链地址法（或拉链法）解决hash冲突
//

// 完成primary_hashtable的初始化
void assoc_init(const int hashpower_init);
// 根据key和nkey查找对应的item
item *assoc_find(const char *key, const size_t nkey, const uint32_t hv);
// 将item插入hashtable中
int assoc_insert(item *item, const uint32_t hv);
// 从hashtable中删除key对应的item
void assoc_delete(const char *key, const size_t nkey, const uint32_t hv);
// 函数没有实现
void do_assoc_move_next_bucket(void);
// 下面两个函数分别为启动和结束hashtable维护的线程，如果不需要这个功能，可以不调用，就是会浪费primary_hashtable指针数组占用的内存资源
int start_assoc_maintenance_thread(void);
void stop_assoc_maintenance_thread(void);
extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;
