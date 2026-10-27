#ifndef NB_ALLOC_H
#define NB_ALLOC_H

/*
 * 全局内存分配器与统计（alloc）。
 *
 * 该模块不依赖 rpc/ntk，供 nb_base 和第三方独立使用；对外符号为 nb_malloc/
 * nb_free（已带语义，不随文件名变化）。
 * allocator 的安装应在创建线程之前完成；运行期热路径只读当前配置。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NB_MEM_ALIGN_DEFAULT 16u

typedef struct nb_allocator {
    void *(*alloc)(size_t size, void *opaque);
    void (*free)(void *ptr, void *opaque);
    void *opaque;
} nb_allocator_t;

typedef struct nb_mem_stats {
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t failed_alloc_count;
    uint64_t outstanding_bytes;
    uint64_t peak_bytes;
} nb_mem_stats_t;

/* NULL 恢复默认 malloc/free。返回 NB_OK 或 NB_ERR_INVALID。 */
int nb_mem_set_allocator(const nb_allocator_t *allocator);

void *nb_malloc(size_t size);
void *nb_malloc0(size_t size);
void *nb_malloc_aligned(size_t size, size_t alignment);
void *nb_realloc(void *ptr, size_t size);
void nb_free(void *ptr);

void nb_mem_get_stats(nb_mem_stats_t *out);
void nb_mem_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* NB_ALLOC_H */
