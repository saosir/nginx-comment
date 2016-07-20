
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LIST_H_INCLUDED_
#define _NGX_LIST_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

typedef struct ngx_list_part_s  ngx_list_part_t;

struct ngx_list_part_s {
    void             *elts; 	//指向一块连续的内存，有n个元素
    ngx_uint_t        nelts; 	//elts已经使用了多少个元素,当elts==n会新创建一个链表节点
    ngx_list_part_t  *next; 	//下一个链表节点
};


/*
每个链表节点包含一个数组,每次分配一个数组的元素给用户，
元素大小以及数组元素个数由用户指定，当数组元素使用完
之后就会新创建一个链表节点


链表初始化的时候的参数:
1. size 单个分配出去的元素的内存大小,即ngx_list_part_s::elts中一个元素的大小
2. n 表示ngx_list_part_s::elts中有多少个元素
3. pool用于分配内存的内存池







last: 指向该链表的最后一个节点,用于分配节点出去

part: 该链表的首个存放具体元素的节点

size: 链表中存放的具体元素所需内存大小,ngx_list_part_s::elts元素大小

nalloc: 每个链表节点中数组元素的容量,nalloc >= ngx_list_part_s::nelts

pool: 该list使用的分配内存的pool

*/

typedef struct {
    ngx_list_part_t  *last;
    ngx_list_part_t   part;
    size_t            size; // 元素大小
    ngx_uint_t        nalloc;
    ngx_pool_t       *pool;
} ngx_list_t;

// 链表每个节点都是一个数组，元素存放于数组当中
ngx_list_t *ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size);

// n 链表节点数组大小
// size 元素大小
static ngx_inline ngx_int_t
ngx_list_init(ngx_list_t *list, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    list->part.elts = ngx_palloc(pool, n * size);
    if (list->part.elts == NULL) {
        return NGX_ERROR;
    }

    list->part.nelts = 0;
    list->part.next = NULL;
    list->last = &list->part;
    list->size = size;
    list->nalloc = n;
    list->pool = pool;

    return NGX_OK;
}


/*
 *
 *  the iteration through the list:
 *
 *  part = &list.part;
 *  data = part->elts;
 *
 *  for (i = 0 ;; i++) {
 *
 *      if (i >= part->nelts) {
 *          if (part->next == NULL) {
 *              break;
 *          }
 *
 *          part = part->next;
 *          data = part->elts;
 *          i = 0;
 *      }
 *
 *      ...  data[i] ...
 *
 *  }
 */


void *ngx_list_push(ngx_list_t *list);


#endif /* _NGX_LIST_H_INCLUDED_ */
