
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

/*
内存池初始化的时候传入的参数size表示pagesize，分配内存的时候
会根据这个pagesize进行分配，内存池的部分数据结构存储在这个
pagesize大小的内存头部，内存池是一个链表，将多个pagesize大小的
内存块链接成链表，为了存储一些内存池相关信息所以链表头
会比普通元素多占用一些额外字节,但是链表头与普通链表元素
所在的内存块的大小都是一样的大小为pagesize

*/
/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;
    void                 *data;
    ngx_pool_cleanup_t   *next;
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc;
};

/*
last：	是一个unsigned char 类型的指针，保存的是/当前内存池分配到
		末位地址，即下一次分配从此处开始。

end：内存池结束位置；

next：	内存池里面有很多块内存，这些内存块就是通过该指针连
		成链表的，next指向下一块内存。

failed：内存池分配失败次数。

*/
typedef struct {
    u_char               *last; // 当前分配到内存哪个位置，last之前的内存是已分配出去
    u_char               *end;  // 内存页尾部，end-last可以知道还剩余多少内存可以使用
    ngx_pool_t           *next; // 指向下一个内存页
    ngx_uint_t            failed; // 改内存页分配失败次数
} ngx_pool_data_t;


/*
d：内存池的数据块；

max：内存池数据块的最大值；

current：指向当前内存池；

chain：该指针挂接一个ngx_chain_t结构；

large：大块内存链表，即分配空间超过max的情况使用；

cleanup：释放内存池的callback

log：日志信息

*/
struct ngx_pool_s {
    ngx_pool_data_t       d;
	// 链表头比普通元素占用的额外字节数据，下面的字段只有
	// 内存池链表头拥有，用于管理内存池数据结构，非链表头
	// 节点只有ngx_pool_data_t字段，参看ngx_palloc_block

	// 内存池最大分配限额，超过这个值调用ngx_palloc_large进行分配
    size_t                max; 
	// 当前使用的内存页
    ngx_pool_t           *current;
	// 与ngx_buf相关
    ngx_chain_t          *chain;
	//申请大于max的内存块统一放到large链表中
    ngx_pool_large_t     *large;
	// 资源释放句柄
    ngx_pool_cleanup_t   *cleanup;
    ngx_log_t            *log;
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
