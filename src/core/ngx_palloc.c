
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;

    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;
    }

    p->d.last = (u_char *) p + sizeof(ngx_pool_t); // 内存前部存储的是内存池数据结构
    p->d.end = (u_char *) p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t); // 减去头部结构体大小，得到可用内存大小
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}


void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

    for (l = pool->large; l; l = l->next) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);

        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we cannot use this log while free()ing the pool
     */

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);

        if (n == NULL) {
            break;
        }
    }
}

// 重置内存池，将大块内存归还系统，内存页重新设置
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

	// 归还大块内存给系统
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    pool->large = NULL;
	// 内存页重新设置
    for (p = pool; p; p = p->d.next) {
		// 这里有个问题，所有的内存页都偏移sizeof(ngx_pool_t)大小吗?
		// 不是只有链表头才需要偏移sizeof(ngx_pool_t)大小?
		// 依小人拙见，下面一行代码改为
		// p->d.last = (u_char *) p +  (p == pool ? sizeof(ngx_pool_t ) : sizeof(ngx_pool_data_t));
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    }
}

// 从内存池中分配内存
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {

        p = pool->current;

        do {
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT); //降低CPU读取内存的次数，提 高性能

            if ((size_t) (p->d.end - m) >= size) { //剩余的内存够分配
                p->d.last = m + size;				

                return m;
            }

            p = p->d.next; // 这个链表节点的内存不够分配，指向下一个

        } while (p);

        return ngx_palloc_block(pool, size); //当前的链表节点不能满足分配需求，
        									//创建一个节点并分配内存
    }

    return ngx_palloc_large(pool, size);
}


//此函数分配的内存并没有像上面的函数那样进行过对齐
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {

        p = pool->current;

        do {
            m = p->d.last;

            if ((size_t) (p->d.end - m) >= size) {
				// 该内存页满足分配需求
                p->d.last = m + size;

                return m;
            }
			// 不满足分配需求，试探下一个内存页是否满足
            p = p->d.next;

        } while (p);
		// 当前的内存页都不满足分配需求的情况下，就向系统开辟一个新
		// 的内存页，从新的内存页中分配内存
        return ngx_palloc_block(pool, size);
    }
	// 申请的内存比较大
    return ngx_palloc_large(pool, size);
}

/* 内存不足的情况新建一个内存页链接到内存池*/
static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new, *current;
	//重新分配一块与头链表块相同大小的内存
    psize = (size_t) (pool->d.end - (u_char *) pool);

    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log); //调用的是ngx_alloc(size, log)
    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t); //非头结点只存储链表的节点信息，偏移sizeof(ngx_pool_data_t)大小即可
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size; // size是要分配出去的内存大小，因此在这里将d.last预先偏移size

    current = pool->current;
	//得到最后一个节点指针p，将新开辟的内存页串接到链表尾部，即p之后
    for (p = current; p->d.next; p = p->d.next) {
		// 遍历过程中顺便将所有链表节点的失败次数都添加1
		// 因为前面的内存页分配失败才会执行到这个函数
        if (p->d.failed++ > 4) {		
			//并取得最后一个内存申请失败次数大于5的节点的*后一个节点*
			//这样current指向的节点的d.failed <= 5
            current = p->d.next;		
        }
    }

    p->d.next = new; //插入尾部

    pool->current = current ? current : new;

    return m;
}


// 当申请的内存大于max的时候，会调用这个函数
static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;
	// 直接调用c语言malloc
    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
			// 说明有个可以复用的链表节点来挂接内存块
			// 不需要向内存池申请一个链表节点内存
            large->alloc = p;
            return p;
        }

        if (n++ > 3) {//只复用前面4个节点，why?防止遍历耗时太长
            break;
        }
    }
	// 将这个大内存块放到large链表中进行管理
    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}

// 功能与ngx_palloc_large基本相同，只是直接分配一个large链表节点挂接内存块
// 按照指定对齐大小alignment来申请一块大小为size的内存
// 此处获取的内存不管大小都将被置于大内存块链中管理。

void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;
	// 在large链表中查找确认p是否是大块内存
	// 如果是大块内存，将对应链表节点的alloc字段赋值NULL
	// 并调用C语言free函数释放内存
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }
	// 小块内存不做处理，留在内存池中，内存池销毁的时候自然
	// 会释放对应内存，这里是我认为不好的地方，有利有弊，好处
	// 是减轻程序员管理内存的负担，直接释放内存池就统一释放
	// 所有的内存，不要一个一个去跟踪，坏处就是不使用某个内存
	// 的时候没能及时释放归还给系统
    return NGX_DECLINED;
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}

// ngx_pool_t中的cleanup字段管理着一个特殊的链表，
// 该链表的每一项都记录着一个特殊的需要释放的资源
// ngx_pool_t不仅仅可以管理内存，通过这个机制，
// 也可以管理任何需要释放的资源，
// 例如，关闭文件，或者删除文件等

// size就是要存储这个data字段所指向的资源的大小
// 比如我们需要最后删除一个文件，那我们在调
// 用这个函数的时候，把size指定为存储文件名的
// 字符串的大小

ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;

    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }
	// 申请资源内存
    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

	//头插法
    c->handler = NULL;
    c->next = p->cleanup;

    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


// 直接调用内存池中文件清理函数
void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}

// 清理文件回调
void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);
	// 关闭文件
    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}

// 删除文件回调
void
ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);
	// unlink
    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }
	// close
    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
