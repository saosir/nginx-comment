
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
    // slab指明分配的chunk大小为2^slab
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
