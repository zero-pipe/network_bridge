#ifndef NB_METRIC_MACROS_H
#define NB_METRIC_MACROS_H

/*
 * rpc 侧原子/指标入口。
 *
 * 通用原子原语（u32/u64、acquire/release、CAS、spinlock、refcount）统一放在
 * nb_base 的 atomic.h，nb_base 与 rpc 共用同一份实现，避免重复定义。
 * 本头只保留 rpc 特有的 metrics 开关与统计宏。
 */
#include "atomic.h"

/*
 * metrics 开关：运行期可关（node 配置 metrics.disabled），也可在编译期用
 * RPC_METRICS_ENABLED=0 彻底编译掉。关闭时热路径只多一次全局标志判断。
 */
#ifndef RPC_METRICS_ENABLED
#define RPC_METRICS_ENABLED 1
#endif

extern volatile int nb_metrics_enabled;
extern volatile int nb_latency_enabled;
extern volatile uint32_t nb_metrics_sample_every;

#if RPC_METRICS_ENABLED
/* nb_metrics_t 全是 uint64_t 计数；下面三个宏让调用点保持简短。 */
#define NB_METRIC_INC(field)                             \
    do {                                                  \
        if (nb_metrics_enabled) {                      \
            nb_atomic_add_u64((uint64_t *)&(field), 1u); \
        }                                                 \
    } while (0)
#define NB_METRIC_ADD(field, v)                                     \
    do {                                                             \
        if (nb_metrics_enabled) {                                 \
            nb_atomic_add_u64((uint64_t *)&(field), (uint64_t)(v)); \
        }                                                            \
    } while (0)
#define NB_METRIC_SUB(field, v)                                     \
    do {                                                             \
        if (nb_metrics_enabled) {                                 \
            nb_atomic_sub_u64((uint64_t *)&(field), (uint64_t)(v)); \
        }                                                            \
    } while (0)
#else
#define NB_METRIC_INC(field) ((void)0)
#define NB_METRIC_ADD(field, v) ((void)0)
#define NB_METRIC_SUB(field, v) ((void)0)
#endif

#endif /* NB_METRIC_MACROS_H */
