#ifndef NB_BUF_H
#define NB_BUF_H

/*
 * 所有权明确的连续消息缓冲。
 *
 * unique 模式：默认路径，无原子操作；用 nb_buf_move() 明确转移所有权，
 * 用 nb_buf_release() 归还池或释放。
 *
 * shared 模式：仅在真正的一对多广播前调用 nb_buf_share() 提升，
 * 之后用 nb_buf_ref()/nb_buf_ref_n() 分发，最后由每个持有者 release。
 *
 * 该模块不依赖 rpc/ntk，供 nb_base 和第三方独立使用。
 */

#include <stddef.h>
#include <stdint.h>

#include "alloc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nb_buf nb_buf_t;
typedef struct nb_buf_pool nb_buf_pool_t;
typedef void (*nb_buf_free_fn)(void *opaque, uint8_t *data);

#define NB_BUF_FLAG_SHARED   0x0001u
#define NB_BUF_FLAG_EXTERNAL 0x0002u
#define NB_BUF_FLAG_READONLY 0x0004u
#define NB_BUF_FLAG_POOLED   0x0008u

/* 池配置：惰性分档池按需创建 slot；需配合 slot_count > 0 作为硬上限。 */
#define NB_BUF_POOL_FLAG_LAZY 0x0001u

typedef struct nb_buf_view {
    nb_buf_t *owner; /* 借用，不增加引用 */
    const uint8_t *data;
    size_t size;
} nb_buf_view_t;

typedef struct nb_buf_pool_config {
    size_t slot_size;
    size_t slot_count;  /* 0 = 懒分配；>0 = 预分配并作为硬上限 */
    size_t max_cached;  /* 0 = slot_count 或默认 64 */
    unsigned flags;
    const nb_allocator_t *allocator; /* 预留；当前使用全局 allocator */
} nb_buf_pool_config_t;

typedef struct nb_buf_pool_stats {
    uint64_t hits;
    uint64_t misses;
    uint64_t failed;
    uint64_t outstanding;
    uint64_t cached;
    uint64_t created;
} nb_buf_pool_stats_t;

nb_buf_t *nb_buf_alloc(size_t size);
nb_buf_t *nb_buf_alloc_aligned(size_t size, size_t alignment);
nb_buf_t *nb_buf_create_external(void *data,
                                 size_t size,
                                 nb_buf_free_fn free_fn,
                                 void *opaque,
                                 unsigned flags);

/* unique 所有权转移；成功后把 *buf 置 NULL。 */
nb_buf_t *nb_buf_move(nb_buf_t **buf);
/* unique 直接回收；shared 减少引用并由最后一个持有者回收。 */
void nb_buf_release(nb_buf_t *buf);

/* 由唯一所有者调用，把 unique 提升为 shared；重复调用返回 NB_ERR_ALREADY。 */
int nb_buf_share(nb_buf_t *buf);
/* 仅 shared 可用；unique 返回 NULL。 */
nb_buf_t *nb_buf_ref(nb_buf_t *buf);
/* 一次性增加 n 个引用，返回 buf；仅 shared 可用。 */
nb_buf_t *nb_buf_ref_n(nb_buf_t *buf, size_t n);

uint8_t *nb_buf_data(nb_buf_t *buf);
const uint8_t *nb_buf_const_data(const nb_buf_t *buf);
size_t nb_buf_size(const nb_buf_t *buf);
size_t nb_buf_capacity(const nb_buf_t *buf);
unsigned nb_buf_flags(const nb_buf_t *buf);
int nb_buf_is_shared(const nb_buf_t *buf);
int nb_buf_set_size(nb_buf_t *buf, size_t size);

/* 借用切片；owner 必须比 view 存活更久。 */
int nb_buf_slice(nb_buf_t *owner, size_t offset, size_t size, nb_buf_view_t *out);

nb_buf_pool_t *nb_buf_pool_create(const nb_buf_pool_config_t *config);
nb_buf_t *nb_buf_acquire(nb_buf_pool_t *pool);
nb_buf_t *nb_buf_pool_acquire(nb_buf_pool_t *pool);
void nb_buf_pool_uninit(nb_buf_pool_t **pool);
void nb_buf_pool_get_stats(const nb_buf_pool_t *pool, nb_buf_pool_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NB_BUF_H */
