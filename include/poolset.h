#ifndef NB_POOLSET_H
#define NB_POOLSET_H

/*
 * nb_poolset：按尺寸分档的 nb_buf 池。
 *
 * 用于 RX 热路径：每次 IO 取一份 capacity >= size 的 buffer，命中最小的可用
 * 分档，耗尽时回退到更大分档，避免退化成裸 malloc。
 *
 * 底层定长 nb_buf 池 nb_buf_pool 与 nb_buf 同处 buf.h（两者强耦合）。
 */

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nb_poolset nb_poolset_t;

/* slot_counts[i] 是 slot_sizes[i] 分档的硬上限；slot_sizes 必须升序。 */
nb_poolset_t *nb_poolset_create(const size_t *slot_sizes,
                                const size_t *slot_counts,
                                size_t class_count);

/* 取一个 capacity >= size 的 buffer；无可用分档时返回 NULL。 */
nb_buf_t *nb_poolset_acquire(nb_poolset_t *set, size_t size);

/* 汇总各分档的 hits/misses/outstanding/created 等统计，用于诊断与调参。 */
void nb_poolset_get_stats(const nb_poolset_t *set, nb_buf_pool_stats_t *out);

void nb_poolset_uninit(nb_poolset_t **set);

#ifdef __cplusplus
}
#endif

#endif /* NB_POOLSET_H */