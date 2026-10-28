#ifndef NB_TRANSPORT_PROVIDER_H
#define NB_TRANSPORT_PROVIDER_H

#include <message.h>
#include <qos.h>
#include <types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nb_transport_provider nb_transport_provider_t;
typedef struct nb_transport_instance nb_transport_instance_t;

/* ---- 消息与回调 ---- */
/* Provider SPI uses the same message type as core. */
typedef nb_message_t nb_transport_message_t;

typedef void (*transport_receive_fn)(void *receiver,
                                     void *reply_token,
                                     const nb_transport_message_t *message);

/* ---- Provider 生命周期与操作 ---- */
typedef struct nb_transport_provider_options {
    const char *scheme;
    const char *endpoint;
    void *receiver;
    transport_receive_fn on_receive;
    nb_intra_process_t intra_process;
    nb_qos_t qos;
} nb_transport_provider_options_t;

/* send() returning NB_OK means the provider accepted the message for delivery. */
typedef struct nb_transport_provider_ops {
    int (*create)(const nb_transport_provider_options_t *options, nb_transport_instance_t **out);
    int (*start)(nb_transport_instance_t *instance);
    void (*stop)(nb_transport_instance_t *instance);
    void (*destroy)(nb_transport_instance_t *instance);
    int (*send)(nb_transport_instance_t *instance,
                const nb_endpoint_t *target,
                const nb_transport_message_t *message);
    int (*reply)(nb_transport_instance_t *instance,
                 void *reply_token,
                 const nb_endpoint_t *peer,
                 const nb_transport_message_t *message);
    /*
     * Optional topic/service registration hooks for transports that need
     * explicit subscribers (for example DDS topics). Providers that do not
     * need them may leave these NULL.
     */
    int (*subscribe)(nb_transport_instance_t *instance,
                     const char *service,
                     const char *method);
    void (*unsubscribe)(nb_transport_instance_t *instance,
                        const char *service,
                        const char *method);
} nb_transport_provider_ops_t;

struct nb_transport_provider {
    const char *scheme;
    const nb_transport_provider_ops_t *ops;
};

/* ---- 注册 ---- */
/* Register before creating nodes. Provider descriptors and ops must have static lifetime. */
int nb_transport_provider_register(const nb_transport_provider_t *provider);

#ifdef __cplusplus
}
#endif

#endif /* NB_TRANSPORT_PROVIDER_H */
