
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


void *
ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len)
{
    ngx_uint_t       i;
    ngx_hash_elt_t  *elt;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "hf:\"%*s\"", len, name);
#endif

    elt = hash->buckets[key % hash->size];

    if (elt == NULL) {
        return NULL;
    }

    while (elt->value) { // 数组尾部的value是NULL指针，value在结构体的第一个字段
        if (len != (size_t) elt->len) {
            goto next;
        }
		// 比较key
        for (i = 0; i < len; i++) {
            if (name[i] != elt->name[i]) {
                goto next;
            }
        }

        return elt->value;

    next:
		// 连续数组，指向下一个元素
        elt = (ngx_hash_elt_t *) ngx_align_ptr(&elt->name[0] + elt->len,
                                               sizeof(void *));
        continue;
    }

    return NULL;
}

// 找不到返回hwc->value
void *
ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len)
{
    void        *value;
    ngx_uint_t   i, n, key;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "wch:\"%*s\"", len, name);
#endif
	// 从尾部开始往前找，则n 到 len-1 即为关键字中最后一个 子关键字
    n = len;

    while (n) {
        if (name[n - 1] == '.') {
            break;
        }

        n--;
    }

    key = 0;

	// 计算n到len-1字符串hash值
    for (i = n; i < len; i++) {
        key = ngx_hash(key, name[i]);
    }


	//调用普通查找找到关键字的value（用户自定义数据指针）

    value = ngx_hash_find(&hwc->hash, key, &name[n], len - n);

/**
* 用value指针低2位来携带信息，如下：

* 00 - value 是 "example.com" 和 "*.example.com"的数据指针

* 01 - value 仅仅是 "*.example.com"的数据指针

* 10 - value 是 支持通配符哈希表是 "example.com" 和 "*.example.com" 指针

* 11 - value 仅仅是 "*.example.com"的指针

*/


    if (value) {

        /* 注意指针和数据指针，data pointer 和pointer的区别
         * the 2 low bits of value have the special meaning:
         *     00 - value is data pointer for both "example.com"
         *          and "*.example.com";
         *     01 - value is data pointer for "*.example.com" only;
         *     10 - value is pointer to wildcard hash allowing
         *          both "example.com" and "*.example.com";
         *     11 - value is pointer to wildcard hash allowing
         *          "*.example.com" only.
         */

        if ((uintptr_t) value & 2) {

            if (n == 0) { // n==0完全匹配尾缀

                /* "example.com" */

                if ((uintptr_t) value & 1) {
                    return NULL;
                }

                hwc = (ngx_hash_wildcard_t *)
                                          ((uintptr_t) value & (uintptr_t) ~3);
                return hwc->value;
            }

            hwc = (ngx_hash_wildcard_t *) ((uintptr_t) value & (uintptr_t) ~3);

            value = ngx_hash_find_wc_head(hwc, name, n - 1); // -1去掉.符号，递归往下查找

            if (value) {
                return value;
            }

            return hwc->value;
        }

        if ((uintptr_t) value & 1) {

            if (n == 0) {

                /* "example.com" */

                return NULL;
            }

            return (void *) ((uintptr_t) value & (uintptr_t) ~3);
        }

        return value;
    }
    return hwc->value;
}


void *
ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len)
{
    void        *value;
    ngx_uint_t   i, key;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "wct:\"%*s\"", len, name);
#endif

    key = 0;

    for (i = 0; i < len; i++) {
        if (name[i] == '.') {
            break;
        }

        key = ngx_hash(key, name[i]);
    }

    if (i == len) {
        return NULL;
    }

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "key:\"%ui\"", key);
#endif

    value = ngx_hash_find(&hwc->hash, key, name, i);

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "value:\"%p\"", value);
#endif

    if (value) {

        /*
         * the 2 low bits of value have the special meaning:
         *     00 - value is data pointer;
         *     11 - value is pointer to wildcard hash allowing "example.*".
         */

        if ((uintptr_t) value & 2) {

            i++;

            hwc = (ngx_hash_wildcard_t *) ((uintptr_t) value & (uintptr_t) ~3);

            value = ngx_hash_find_wc_tail(hwc, &name[i], len - i);

            if (value) {
                return value;
            }

            return hwc->value;
        }

        return value;
    }

    return hwc->value;
}

// 先在普通哈希表中查找，没找到再去前置通配符哈希表中查找，
// 最后去后置通配符哈希表中查找

void *
ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key, u_char *name,
    size_t len)
{
    void  *value;

    if (hash->hash.buckets) {
        value = ngx_hash_find(&hash->hash, key, name, len);

        if (value) {
            return value;
        }
    }

    if (len == 0) {
        return NULL;
    }

    if (hash->wc_head && hash->wc_head->hash.buckets) {
        value = ngx_hash_find_wc_head(hash->wc_head, name, len);

        if (value) {
            return value;
        }
    }

    if (hash->wc_tail && hash->wc_tail->hash.buckets) {
        value = ngx_hash_find_wc_tail(hash->wc_tail, name, len);

        if (value) {
            return value;
        }
    }

    return NULL;
}


// 计算ngx_hash_elt_t占用内存大小

// 注意哈希元素的键字节对齐ngx_align((name)->key.len + 2, sizeof(void *))
// 保存一个指针和一个键
// sizeof(void*)存储value，2存储len，key.len存储key
#define NGX_HASH_ELT_SIZE(name)                                               \
    (sizeof(void *) + ngx_align((name)->key.len + 2, sizeof(void *)))

ngx_int_t
ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names, ngx_uint_t nelts)
{
    u_char          *elts;
    size_t           len;
    u_short         *test; // 该数组在不同位置表示不同含义
    ngx_uint_t       i, n, key, size, start, bucket_size;
    ngx_hash_elt_t  *elt, **buckets;
	//一个桶可以存储多个元素，桶使用的不是开链法
    for (n = 0; n < nelts; n++) {
		// 每个元素占用空间小于桶元素空间大小
        if (hinit->bucket_size < NGX_HASH_ELT_SIZE(&names[n]) + sizeof(void *))
        {
            ngx_log_error(NGX_LOG_EMERG, hinit->pool->log, 0,
                          "could not build the %s, you should "
                          "increase %s_bucket_size: %i",
                          hinit->name, hinit->name, hinit->bucket_size);
            return NGX_ERROR;
        }
    }

	// 找到合适的桶数量
    test = ngx_alloc(hinit->max_size * sizeof(u_short), hinit->pool->log);
    if (test == NULL) {
        return NGX_ERROR;
    }

    bucket_size = hinit->bucket_size - sizeof(void *); // 每个桶都要保存一个void*指针，其他空间保存元素

	// 计算初始桶的大小，试探桶的数量是否合适

	/* 计算需要桶数目的下界

	每个元素最少需要 NGX_HASH_ELT_SIZE(&name[n]) > (2*sizeof(void*)) 的空间

	因此 bucket_size 大小的桶最多能容下 bucket_size/(2*sizeof(void*)) 个元素

	因此 nelts 个元素就最少需要start个桶。

	*/
    start = nelts / (bucket_size / (2 * sizeof(void *)));
    start = start ? start : 1;

    if (hinit->max_size > 10000 && nelts && hinit->max_size / nelts < 100) {
        start = hinit->max_size - 1000;
    }

	// 从最小桶开始，计算哈希表至少需要多少个桶
    for (size = start; size < hinit->max_size; size++) {

        ngx_memzero(test, size * sizeof(u_short));

        for (n = 0; n < nelts; n++) {
            if (names[n].key.data == NULL) {
                continue;
            }

            key = names[n].key_hash % size;
			// 产生碰撞test[key]大小非0，说明这个桶要存储多个元素
            test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "%ui: %ui %ui \"%V\"",
                          size, key, test[key], &names[n].key);
#endif
			// 保证每个桶小于规定大小，如果超过大小，那么
			// 增加桶数量，减少碰撞
            if (test[key] > (u_short) bucket_size) { // 增加桶数量+1，再计算合适的桶数组大小
                goto next;
            }
        }
		// 已经找到合适的桶数量
        goto found;

    next:

        continue;
    }

    ngx_log_error(NGX_LOG_EMERG, hinit->pool->log, 0,
                  "could not build the %s, you should increase "
                  "either %s_max_size: %i or %s_bucket_size: %i",
                  hinit->name, hinit->name, hinit->max_size,
                  hinit->name, hinit->bucket_size);

    ngx_free(test);

    return NGX_ERROR;

found:
	// size为桶数量

    for (i = 0; i < size; i++) {
        test[i] = sizeof(void *); // 链表尾部NULL指针
    }
	//计算test每个桶占用内存的大小
    for (n = 0; n < nelts; n++) {
        if (names[n].key.data == NULL) {
            continue;
        }
		// 与上面算法一致
        key = names[n].key_hash % size;
        test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));
    }

    len = 0; // 总的内存大小

    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
			// 该桶没有元素
            continue;
        }
		// 向上调整对齐字节
        test[i] = (u_short) (ngx_align(test[i], ngx_cacheline_size));

        len += test[i];
    }
	// ngx_hash_wildcard_t 处理
    if (hinit->hash == NULL) {
        hinit->hash = ngx_pcalloc(hinit->pool, sizeof(ngx_hash_wildcard_t)
                                             + size * sizeof(ngx_hash_elt_t *));
        if (hinit->hash == NULL) {
            ngx_free(test);
            return NGX_ERROR;
        }

        buckets = (ngx_hash_elt_t **)
                      ((u_char *) hinit->hash + sizeof(ngx_hash_wildcard_t));

    } else {
        buckets = ngx_pcalloc(hinit->pool, size * sizeof(ngx_hash_elt_t *));
        if (buckets == NULL) {
            ngx_free(test);
            return NGX_ERROR;
        }
    }

    elts = ngx_palloc(hinit->pool, len + ngx_cacheline_size);
    if (elts == NULL) {
        ngx_free(test);
        return NGX_ERROR;
    }

    elts = ngx_align_ptr(elts, ngx_cacheline_size);
	// 将每个桶指向对应的内存区，可以
	// 发现，哈希表中的元素是在一块连续
	// 内存中，只是将这块连续内存根据
	// 桶来划分，某块内存属于某个桶，
	// 块内存大小由每个桶保存的元素多少
	// 来决定
    for (i = 0; i < size; i++) {
		// 说明该桶没有元素
        if (test[i] == sizeof(void *)) {
            continue;
        }

        buckets[i] = (ngx_hash_elt_t *) elts;
        elts += test[i]; // 偏移到下一个桶的起始位置，test[i]预先已经计算好

    }

    for (i = 0; i < size; i++) {
        test[i] = 0;
    }

	// 将元素拷贝到哈希表中
	// test[i]表示某个桶已经使用的内存大小，
	// 类似与开链法中的桶头指针
    for (n = 0; n < nelts; n++) {
        if (names[n].key.data == NULL) {
            continue;
        }

        key = names[n].key_hash % size;
        elt = (ngx_hash_elt_t *) ((u_char *) buckets[key] + test[key]);

        elt->value = names[n].value;
        elt->len = (u_short) names[n].key.len;

        ngx_strlow(elt->name, names[n].key.data, names[n].key.len);
		// 该桶下一个元素存储的起始指针
        test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));
    }

    for (i = 0; i < size; i++) {
		// 该桶没有元素
        if (buckets[i] == NULL) {
            continue;
        }
		// 尾部是NULL，判断数组结束
        elt = (ngx_hash_elt_t *) ((u_char *) buckets[i] + test[i]);

        elt->value = NULL;
    }

    ngx_free(test);

    hinit->hash->buckets = buckets;
    hinit->hash->size = size;

#if 0

    for (i = 0; i < size; i++) {
        ngx_str_t   val;
        ngx_uint_t  key;

        elt = buckets[i];

        if (elt == NULL) {
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "%ui: NULL", i);
            continue;
        }

        while (elt->value) {
            val.len = elt->len;
            val.data = &elt->name[0];

            key = hinit->key(val.data, val.len);

            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "%ui: %p \"%V\" %ui", i, elt, &val, key);

            elt = (ngx_hash_elt_t *) ngx_align_ptr(&elt->name[0] + elt->len,
                                                   sizeof(void *));
        }
    }

#endif

    return NGX_OK;
}


/*hinit为初始化结构体指针，names为预加入哈希表数组，elts为预加入数组大小

特别要注意的是这里的key已经都是被预处理过的。例如：“*.abc.com”或者“.abc.com”被预处理完成以后，

变成了“com.abc.”。而“mail.xxx.*”则被预处理为“mail.xxx.”
*/
/*
nginx为了处理带有通配符的域名的匹配问题，实现了ngx_hash_wildcard_t这样的hash表。
他可以支持两种类型的带有通配符的域名。一种是通配符在前的，
例如：“*.abc.com”，也可以省略掉星号，直接写成”.abc.com”。这样的key，
可以匹配www.abc.com，qqq.www.abc.com之类的。另外一种是通配符在末尾的，
例如：“mail.xxx.*”，请特别注意通配符在末尾的不像位于开始的通配符
可以被省略掉。这样的通配符，可以匹配mail.xxx.com、mail.xxx.com.cn、mail.xxx.net之
类的域名。

有一点必须说明，就是一个ngx_hash_wildcard_t类型的hash表只能包含通配符在前
的key或者是通配符在后的key。不能同时包含两种类型的通配符的key。
ngx_hash_wildcard_t类型变量的构建是通过函数ngx_hash_wildcard_init完成的，
而查询是通过函数ngx_hash_find_wc_head或者ngx_hash_find_wc_tail来做的。
ngx_hash_find_wc_head是查询包含通配符在前的key的hash表的，
而ngx_hash_find_wc_tail是查询包含通配符在后的key的hash表的


构造此hash表的所有的通配符key的数组。
特别要注意的是这里的key已经都是被预处理过的。
例如：“*.abc.com”或者“.abc.com”被预处理完成以后，
变成了“com.abc.”。而“mail.xxx.*”则被预处理为“mail.xxx.”。
为什么会被处理这样？这里不得不简单地描述一下通配符hash表的实现原理。
当构造此类型的hash表的时候，实际上是构造了一个hash表的一个“链表”，
是通过hash表中的key“链接”起来的。比如：对于“*.abc.com”将会构造出2个hash表，
第一个hash表中有一个key为com的表项，该表项的value包含有指向第二个hash表的指针，
而第二个hash表中有一个表项abc，该表项的value包含有指向*.abc.com对应的value的指针。
那么查询的时候，比如查询www.abc.com的时候，先查com，通过查com可以找到第二级的
hash表，在第二级hash表中，再查找abc，依次类推，直到在某一级的hash表中查到的
表项对应的value对应一个真正的值而非一个指向下一级hash表的指针的时候，
查询过程结束。这里有一点需要特别注意的，就是names数组中元素的value值低
两位bit必须为0（有特殊用途）。如果不满足这个条件，这个hash表查询不出正
确结果。

由于指针都字节对齐了，低4位肯定为0，
这种操作（name->value = (void *) ((uintptr_t) wdc | (dot ? 3 : 2)) ） ,
使用了指针的低位携带额外信息，节省了内存

*/
ngx_int_t
ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts)
{
    size_t                len, dot_len;
    ngx_uint_t            i, n, dot;
    ngx_array_t           curr_names, next_names;
    ngx_hash_key_t       *name, *next_name;
    ngx_hash_init_t       h;
    ngx_hash_wildcard_t  *wdc;

    if (ngx_array_init(&curr_names, hinit->temp_pool, nelts,
                       sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&next_names, hinit->temp_pool, nelts,
                       sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (n = 0; n < nelts; n = i) {

#if 0
        ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                      "wc0: \"%V\"", &names[n].key);
#endif

        dot = 0;
		//www.example.com 被预处理为com.example.www
        for (len = 0; len < names[n].key.len; len++) {
            if (names[n].key.data[len] == '.') {
                dot = 1;
                break;
            }
        }
		//将关键字dot以前的关键字放入curr_names，加入www.example.com，则name为com
        name = ngx_array_push(&curr_names);
        if (name == NULL) {
            return NGX_ERROR;
        }

        name->key.len = len;
        name->key.data = names[n].key.data;
        name->key_hash = hinit->key(name->key.data, name->key.len); // 计算hash值
        name->value = names[n].value;

		
        dot_len = len + 1;
		//len指向dot后剩余关键字
        if (dot) {
            len++;
        }

        next_names.nelts = 0;
		
		//如果names[n] dot后还有剩余关键字，将剩余关键字放入next_names中
        if (names[n].key.len != len) {
            next_name = ngx_array_push(&next_names);
            if (next_name == NULL) {
                return NGX_ERROR;
            }
			//www.example
            next_name->key.len = names[n].key.len - len; // dot后面字符长度
            next_name->key.data = names[n].key.data + len; // 指向dot后面的数据
            next_name->key_hash = 0;
            next_name->value = names[n].value;

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "wc2: \"%V\"", &next_name->key);
#endif
        }
		//从n+1遍历names，将有相同尾缀的字符放入next_names数组
        for (i = n + 1; i < nelts; i++) {
			// 有相同尾缀
            if (ngx_strncmp(names[n].key.data, names[i].key.data, len) != 0) {
                break;
            }

            if (!dot
                && names[i].key.len > len
                && names[i].key.data[len] != '.')
            {
                break;
            }

            next_name = ngx_array_push(&next_names);
            if (next_name == NULL) {
                return NGX_ERROR;
            }

			// 余下的字符放入next_names
            next_name->key.len = names[i].key.len - dot_len;
            next_name->key.data = names[i].key.data + dot_len;
            next_name->key_hash = 0;
            next_name->value = names[i].value;

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "wc3: \"%V\"", &next_name->key);
#endif
        }
		// 加入上面哈希com.abc.www，经过上面处理next_names处理的是
		// abc.www等
        if (next_names.nelts) {

            h = *hinit;
            h.hash = NULL;
			// 递归创建新的哈希表
            if (ngx_hash_wildcard_init(&h, (ngx_hash_key_t *) next_names.elts,
                                       next_names.nelts)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            wdc = (ngx_hash_wildcard_t *) h.hash;
			// 将用户value值放入新的hash表
            if (names[n].key.len == len) {
                wdc->value = names[n].value;
            }

            name->value = (void *) ((uintptr_t) wdc | (dot ? 3 : 2)); // 在curr_names中

        } else if (dot) {
            name->value = (void *) ((uintptr_t) name->value | 1);
        }
    }
	// 已经构建好二级、三级...哈希表之后，构建底层哈希表
    if (ngx_hash_init(hinit, (ngx_hash_key_t *) curr_names.elts,
                      curr_names.nelts)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

// 哈希函数
ngx_uint_t
ngx_hash_key(u_char *data, size_t len)
{
    ngx_uint_t  i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = ngx_hash(key, data[i]);
    }

    return key;
}

// 字符转小写后哈希
ngx_uint_t
ngx_hash_key_lc(u_char *data, size_t len)
{
    ngx_uint_t  i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = ngx_hash(key, ngx_tolower(data[i]));
    }

    return key;
}

// strlow
ngx_uint_t
ngx_hash_strlow(u_char *dst, u_char *src, size_t n)
{
    ngx_uint_t  key;

    key = 0;

    while (n--) {
        *dst = ngx_tolower(*src);
        key = ngx_hash(key, *dst);
        dst++;
        src++;
    }

    return key;
}

/*

hsize:	将要构建的hash表的桶的个数。对于使用这个结构中包含的信息构建的三种类型的hash表都会使用此参数。
pool:	构建这些hash表使用的pool。
temp_pool:	在构建这个类型以及最终的三个hash表过程中可能用到临时pool。该temp_pool可以在构建完成以后，被销毁掉。这里只是存放临时的一些内存消耗。
keys:	存放所有非通配符key的数组。
keys_hash:	这是个二维数组，第一个维度代表的是bucket的编号，那么keys_hash[i]中存放的是所有的key算出来的hash值对hsize取模以后的值为i的key。假设有3个key,分别是key1,key2和key3假设hash值算出来以后对hsize取模的值都是i，那么这三个key的值就顺序存放在keys_hash[i][0],keys_hash[i][1], keys_hash[i][2]。该值在调用的过程中用来保存和检测是否有冲突的key值，也就是是否有重复。
dns_wc_head:	放前向通配符key被处理完成以后的值。比如：“*.abc.com” 被处理完成以后，变成 “com.abc.” 被存放在此数组中。
dns_wc_tail:	存放后向通配符key被处理完成以后的值。比如：“mail.xxx.*” 被处理完成以后，变成 “mail.xxx.” 被存放在此数组中。
dns_wc_head_hash:
 	该值在调用的过程中用来保存和检测是否有冲突的前向通配符的key值，也就是是否有重复。
dns_wc_tail_hash:
 	该值在调用的过程中用来保存和检测是否有冲突的后向通配符的key值，也就是是否有重复。

*/
ngx_int_t
ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type)
{
    ngx_uint_t  asize;

    if (type == NGX_HASH_SMALL) {
        asize = 4;
        ha->hsize = 107;

    } else {
        asize = NGX_HASH_LARGE_ASIZE;
        ha->hsize = NGX_HASH_LARGE_HSIZE;
    }

    if (ngx_array_init(&ha->keys, ha->temp_pool, asize, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ha->dns_wc_head, ha->temp_pool, asize,
                       sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ha->dns_wc_tail, ha->temp_pool, asize,
                       sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    ha->keys_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize);
    if (ha->keys_hash == NULL) {
        return NGX_ERROR;
    }

    ha->dns_wc_head_hash = ngx_pcalloc(ha->temp_pool,
                                       sizeof(ngx_array_t) * ha->hsize);
    if (ha->dns_wc_head_hash == NULL) {
        return NGX_ERROR;
    }

    ha->dns_wc_tail_hash = ngx_pcalloc(ha->temp_pool,
                                       sizeof(ngx_array_t) * ha->hsize);
    if (ha->dns_wc_tail_hash == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key, void *value,
    ngx_uint_t flags)
{
    size_t           len;
    u_char          *p;
    ngx_str_t       *name;
    ngx_uint_t       i, k, n, skip, last;
    ngx_array_t     *keys, *hwc;
    ngx_hash_key_t  *hk;

    last = key->len;

    if (flags & NGX_HASH_WILDCARD_KEY) {

        /*
         * supported wildcards:
         *     "*.example.com", ".example.com", and "www.example.*"
         */

        n = 0;
		// 检查正确性
        for (i = 0; i < key->len; i++) {

            if (key->data[i] == '*') {
                if (++n > 1) {
                    return NGX_DECLINED;
                }
            }

            if (key->data[i] == '.' && key->data[i + 1] == '.') {
                return NGX_DECLINED;
            }
        }
		//.example.com
        if (key->len > 1 && key->data[0] == '.') {
            skip = 1;
            goto wildcard;
        }

        if (key->len > 2) {
			//*.example.com
            if (key->data[0] == '*' && key->data[1] == '.') {
                skip = 2;
                goto wildcard;
            }
			//www.example.*
            if (key->data[i - 2] == '.' && key->data[i - 1] == '*') {
                skip = 0;
                last -= 2;
                goto wildcard;
            }
        }
		// 不允许统配符在中间
        if (n) {
            return NGX_DECLINED;
        }
    }

    /* exact hash */
	// 没有通配符的元素
    k = 0;

    for (i = 0; i < last; i++) {
        if (!(flags & NGX_HASH_READONLY_KEY)) {
            key->data[i] = ngx_tolower(key->data[i]);
        }
        k = ngx_hash(k, key->data[i]);
    }

    k %= ha->hsize;

    /* check conflicts in exact hash */

    name = ha->keys_hash[k].elts;

    if (name) {
        for (i = 0; i < ha->keys_hash[k].nelts; i++) {
            if (last != name[i].len) {
                continue;
            }
			// 已经存在
            if (ngx_strncmp(key->data, name[i].data, last) == 0) {
                return NGX_BUSY;
            }
        }

    } else {
        if (ngx_array_init(&ha->keys_hash[k], ha->temp_pool, 4,
                           sizeof(ngx_str_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }
	// 放入对应的桶
    name = ngx_array_push(&ha->keys_hash[k]);
    if (name == NULL) {
        return NGX_ERROR;
    }

    *name = *key;

    hk = ngx_array_push(&ha->keys);
    if (hk == NULL) {
        return NGX_ERROR;
    }

    hk->key = *key;
    hk->key_hash = ngx_hash_key(key->data, last);
    hk->value = value;

    return NGX_OK;


wildcard:

    /* wildcard hash */

    k = ngx_hash_strlow(&key->data[skip], &key->data[skip], last - skip);

    k %= ha->hsize;

    if (skip == 1) {

        /* check conflicts in exact hash for ".example.com" */

        name = ha->keys_hash[k].elts;

        if (name) {
            len = last - skip;

            for (i = 0; i < ha->keys_hash[k].nelts; i++) {
                if (len != name[i].len) {
                    continue;
                }

                if (ngx_strncmp(&key->data[1], name[i].data, len) == 0) {
                    return NGX_BUSY;
                }
            }

        } else {
            if (ngx_array_init(&ha->keys_hash[k], ha->temp_pool, 4,
                               sizeof(ngx_str_t))
                != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        name = ngx_array_push(&ha->keys_hash[k]);
        if (name == NULL) {
            return NGX_ERROR;
        }

        name->len = last - 1;
        name->data = ngx_pnalloc(ha->temp_pool, name->len);
        if (name->data == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(name->data, &key->data[1], name->len);
    }


    if (skip) {

        /*
         * convert "*.example.com" to "com.example.\0"
         *      and ".example.com" to "com.example\0"
         */

        p = ngx_pnalloc(ha->temp_pool, last);
        if (p == NULL) {
            return NGX_ERROR;
        }

        len = 0;
        n = 0;

        for (i = last - 1; i; i--) {
            if (key->data[i] == '.') {
                ngx_memcpy(&p[n], &key->data[i + 1], len);
                n += len;
                p[n++] = '.';
                len = 0;
                continue;
            }

            len++;
        }

        if (len) {
            ngx_memcpy(&p[n], &key->data[1], len);
            n += len;
        }

        p[n] = '\0';

        hwc = &ha->dns_wc_head;
        keys = &ha->dns_wc_head_hash[k];

    } else {

        /* convert "www.example.*" to "www.example\0" */

        last++;

        p = ngx_pnalloc(ha->temp_pool, last);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_cpystrn(p, key->data, last);

        hwc = &ha->dns_wc_tail;
        keys = &ha->dns_wc_tail_hash[k];
    }


    /* check conflicts in wildcard hash */

    name = keys->elts;

    if (name) {
        len = last - skip;

        for (i = 0; i < keys->nelts; i++) {
            if (len != name[i].len) {
                continue;
            }

            if (ngx_strncmp(key->data + skip, name[i].data, len) == 0) {
                return NGX_BUSY;
            }
        }

    } else {
        if (ngx_array_init(keys, ha->temp_pool, 4, sizeof(ngx_str_t)) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    name = ngx_array_push(keys);
    if (name == NULL) {
        return NGX_ERROR;
    }

    name->len = last - skip;
    name->data = ngx_pnalloc(ha->temp_pool, name->len);
    if (name->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(name->data, key->data + skip, name->len);


    /* add to wildcard hash */

    hk = ngx_array_push(hwc);
    if (hk == NULL) {
        return NGX_ERROR;
    }

    hk->key.len = last - 1;
    hk->key.data = p;
    hk->key_hash = 0;
    hk->value = value;

    return NGX_OK;
}
