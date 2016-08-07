
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_slab_page_s  ngx_slab_page_t;

struct ngx_slab_page_s {
    // 1.当分配的chunk<128字节，此字段没用，bitmap由内存页分配
    // 2.当分配的chunk==128字节，slab为32位，刚好可以管理整个内存页的32个chunk
    // 3.当分配的chunk>128字节，低8位用来表示分配的chunk大小的位移数，
    // 位移数可以为8、9、10、11，高2字节用来当做bitmap，参考NGX_SLAB_SHIFT_MASK，
    // 高2字节为16位，chunk>128说明chunk最小为256，因为chunk大小按2 幂增长，2k里面
    // 可以换分为16个256字节的chunk，16bit可以满足表示chunk分配情况，前提是chunk大小
    // 大于128byte，参考NGX_SLAB_MAP_MASK表示chunk大于128字节是以后，
    // bitmap掩码
    
    //8位可以用来存储
    uintptr_t         slab;                             
    ngx_slab_page_t  *next;
    // 在 32 位系统中都 是按 4 字节对齐 (4-byte aligned)，
    // 同时共享内存起始地址至少是 4 字节对齐的。
    // 所以 prev的值低2位是0，可用以用存储额外信息。
    uintptr_t         prev;
};


typedef struct {
    ngx_shmtx_sh_t    lock;
    // 2^minshit最小分配的chunk大小
    size_t            min_size;
    size_t            min_shift;
    // 数组
    ngx_slab_page_t  *pages;
    ngx_slab_page_t   free;
    // 内存页起始
    u_char           *start;
    u_char           *end;

    ngx_shmtx_t       mutex;

    u_char           *log_ctx;
    u_char            zero;

    void             *data;
    void             *addr;
} ngx_slab_pool_t;


void ngx_slab_init(ngx_slab_pool_t *pool);
void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
void ngx_slab_free(ngx_slab_pool_t *pool, void *p);
void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);


#endif /* _NGX_SLAB_H_INCLUDED_ */
