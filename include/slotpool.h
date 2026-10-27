#ifndef NB_SLOTPOOL_H
#define NB_SLOTPOOL_H

/*
 * nb_slotpool：按 32-bit slot ID 索引的定长池，带 generation 防 ABA。
 *
 * 用于 chunk / 流式路径：队列只存 ID 不存指针，避免缓冲区被回收后指针悬垂；
 * 每个 slot 底层持有一个定长 nb_buf（见 buf.h）。
 */

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nb_slotpool nb_slotpool_t;
typedef uint32_t nb_slot_id_t;

#define NB_SLOT_ID_NONE 0u

typedef struct nb_slotpool_config {
    size_t payload_size; /* bytes per slot */
    size_t slot_count;   /* hard slot limit (1..65535) */
    unsigned flags;      /* reserved */
} nb_slotpool_config_t;

nb_slotpool_t *nb_slotpool_create(const nb_slotpool_config_t *config);
void nb_slotpool_uninit(nb_slotpool_t **pool);

/* 取一个空闲 slot；池耗尽返回 NB_SLOT_ID_NONE。 */
nb_slot_id_t nb_slotpool_alloc(nb_slotpool_t *pool);

/* 把 ID 解析为借用 buffer；stale/已释放/越界返回 NULL。 */
nb_buf_t *nb_slotpool_get(nb_slotpool_t *pool, nb_slot_id_t id);

/* 归还 slot；stale 或重复释放会被忽略。 */
void nb_slotpool_free(nb_slotpool_t *pool, nb_slot_id_t id);

/* 批量回收（chunk / 批量归还路径）。 */
void nb_slotpool_free_batch(nb_slotpool_t *pool, const nb_slot_id_t *ids, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* NB_SLOTPOOL_H */