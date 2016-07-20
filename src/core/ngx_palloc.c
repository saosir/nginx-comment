
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

    p->d.last = (u_char *) p + sizeof(ngx_pool_t); // �ڴ�ǰ���洢�����ڴ�����ݽṹ
    p->d.end = (u_char *) p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t); // ��ȥͷ���ṹ���С���õ������ڴ��С
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

// �����ڴ�أ�������ڴ�黹ϵͳ���ڴ�ҳ��������
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

	// �黹����ڴ��ϵͳ
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    pool->large = NULL;
	// �ڴ�ҳ��������
    for (p = pool; p; p = p->d.next) {
		// �����и����⣬���е��ڴ�ҳ��ƫ��sizeof(ngx_pool_t)��С��?
		// ����ֻ������ͷ����Ҫƫ��sizeof(ngx_pool_t)��С?
		// ��С��׾��������һ�д����Ϊ
		// p->d.last = (u_char *) p +  (p == pool ? sizeof(ngx_pool_t ) : sizeof(ngx_pool_data_t));
		// �����ʼ���nginx�õ��ظ�

		/*
		A previous attempt to "fix" this can be found here, it looks
		slightly better from my point of view:
		
		http://mailman.nginx.org/pipermail/nginx-devel/2010-June/000351.html
		
		Though we are quite happy with the current code, while it is not
		optimal - it is simple and good enough from practical point of
		view.

		*/
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    }
}

// ���ڴ���з����ڴ�
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {

        p = pool->current;

        do {
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT); //����CPU��ȡ�ڴ�Ĵ������� ������

            if ((size_t) (p->d.end - m) >= size) { //ʣ����ڴ湻����
                p->d.last = m + size;				

                return m;
            }

            p = p->d.next; // ��������ڵ���ڴ治�����䣬ָ����һ��

        } while (p);

        return ngx_palloc_block(pool, size); //��ǰ�������ڵ㲻�������������
        									//����һ���ڵ㲢�����ڴ�
    }

    return ngx_palloc_large(pool, size);
}


//�˺���������ڴ沢û��������ĺ����������й�����
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
				// ���ڴ�ҳ�����������
                p->d.last = m + size;

                return m;
            }
			// ���������������̽��һ���ڴ�ҳ�Ƿ�����
            p = p->d.next;

        } while (p);
		// ��ǰ���ڴ�ҳ��������������������£�����ϵͳ����һ����
		// ���ڴ�ҳ�����µ��ڴ�ҳ�з����ڴ�
        return ngx_palloc_block(pool, size);
    }
	// ������ڴ�Ƚϴ�
    return ngx_palloc_large(pool, size);
}

/* �ڴ治�������½�һ���ڴ�ҳ���ӵ��ڴ��*/
static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new, *current;
	//���·���һ����ͷ��������ͬ��С���ڴ�
    psize = (size_t) (pool->d.end - (u_char *) pool);

    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log); //���õ���ngx_alloc(size, log)
    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t); //��ͷ���ֻ�洢�����Ľڵ���Ϣ��ƫ��sizeof(ngx_pool_data_t)��С����
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size; // size��Ҫ�����ȥ���ڴ��С����������ｫd.lastԤ��ƫ��size

    current = pool->current;
	//�õ����һ���ڵ�ָ��p�����¿��ٵ��ڴ�ҳ���ӵ�����β������p֮��
    for (p = current; p->d.next; p = p->d.next) {
		// ����������˳�㽫���������ڵ��ʧ�ܴ���������1
		// ��Ϊǰ����ڴ�ҳ����ʧ�ܲŻ�ִ�е��������
        if (p->d.failed++ > 4) {		
			//��ȡ�����һ���ڴ�����ʧ�ܴ�������5�Ľڵ��*��һ���ڵ�*
			//����currentָ��Ľڵ��d.failed <= 5
            current = p->d.next;		
        }
    }

    p->d.next = new; //����β��

    pool->current = current ? current : new;

    return m;
}


// ��������ڴ����max��ʱ�򣬻�����������
static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;
	// ֱ�ӵ���c����malloc
    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
			// ˵���и����Ը��õ������ڵ����ҽ��ڴ��
			// ����Ҫ���ڴ������һ�������ڵ��ڴ�
            large->alloc = p;
            return p;
        }

        if (n++ > 3) {//ֻ����ǰ��4���ڵ㣬why?��ֹ������ʱ̫��
            break;
        }
    }
	// ��������ڴ��ŵ�large�����н��й���
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

// ������ngx_palloc_large������ͬ��ֻ��ֱ�ӷ���һ��large�����ڵ�ҽ��ڴ��
// ����ָ�������Сalignment������һ���СΪsize���ڴ�
// �˴���ȡ���ڴ治�ܴ�С���������ڴ��ڴ�����й�����

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
	// ��large�����в���ȷ��p�Ƿ��Ǵ���ڴ�
	// ����Ǵ���ڴ棬����Ӧ�����ڵ��alloc�ֶθ�ֵNULL
	// ������C����free�����ͷ��ڴ�
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }
	// С���ڴ治�������������ڴ���У��ڴ�����ٵ�ʱ����Ȼ
	// ���ͷŶ�Ӧ�ڴ棬����������Ϊ���õĵط��������бף��ô�
	// �Ǽ������Ա�����ڴ�ĸ�����ֱ���ͷ��ڴ�ؾ�ͳһ�ͷ�
	// ���е��ڴ棬��Ҫһ��һ��ȥ���٣��������ǲ�ʹ��ĳ���ڴ�
	// ��ʱ��û�ܼ�ʱ�ͷŹ黹��ϵͳ
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

// ngx_pool_t�е�cleanup�ֶι�����һ�������������
// ��������ÿһ���¼��һ���������Ҫ�ͷŵ���Դ
// ngx_pool_t���������Թ����ڴ棬ͨ��������ƣ�
// Ҳ���Թ����κ���Ҫ�ͷŵ���Դ��
// ���磬�ر��ļ�������ɾ���ļ���

// size����Ҫ�洢���data�ֶ���ָ�����Դ�Ĵ�С
// ����������Ҫ���ɾ��һ���ļ����������ڵ�
// �����������ʱ�򣬰�sizeָ��Ϊ�洢�ļ�����
// �ַ����Ĵ�С

ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;

    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }
	// ������Դ�ڴ�
    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

	//ͷ�巨
    c->handler = NULL;
    c->next = p->cleanup;

    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


// ֱ�ӵ����ڴ�����ļ���������
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

// �����ļ��ص�
void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);
	// �ر��ļ�
    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}

// ɾ���ļ��ص�
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