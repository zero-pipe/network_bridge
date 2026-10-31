/*
 * RTPS transport provider: request/response and event delivery between two
 * provider instances, using explicit unicast discovery so the test does not
 * depend on multicast.
 */
#include <nb/log.h>
#include <nb/message.h>
#include <nb/rtps.h>
#include <nb/transport_provider.h>

#include <ntk.h>

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failures++;                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                              \
    } while (0)

typedef struct peer_ctx {
    const char *name;
    const nb_transport_provider_t *provider;
    nb_transport_instance_t *instance;
    ntk_mutex *lock;
    int requests;
    int responses;
    int events;
    char last_payload[64];
    size_t last_payload_len;
    uint32_t last_request_id;
} peer_ctx_t;

static void peer_record(peer_ctx_t *ctx,
                        const nb_transport_message_t *message,
                        int *counter)
{
    size_t len = message->payload.len;

    ntk_mutex_lock(ctx->lock);
    (*counter)++;
    if (len > sizeof(ctx->last_payload)) {
        len = sizeof(ctx->last_payload);
    }
    if (message->payload.data && len > 0) {
        memcpy(ctx->last_payload, message->payload.data, len);
    }
    ctx->last_payload_len = len;
    ctx->last_request_id = message->request_id;
    ntk_mutex_unlock(ctx->lock);
}

static void on_receive(void *user, void *reply_token, const nb_transport_message_t *message)
{
    peer_ctx_t *ctx = (peer_ctx_t *)user;
    static const char pong[] = "pong";
    nb_transport_message_t response;

    if (!ctx || !message || !message->payload.data) {
        return;
    }
    if (message->kind == NB_MSG_KIND_REQUEST) {
        peer_record(ctx, message, &ctx->requests);
        memset(&response, 0, sizeof(response));
        response.kind = NB_MSG_KIND_RESPONSE;
        response.route = message->route;
        response.request_id = message->request_id;
        response.status = 7;
        response.payload.data = pong;
        response.payload.len = sizeof(pong) - 1u;
        response.payload.encoding = message->payload.encoding;
        (void)ctx->provider->ops->reply(
            ctx->instance, reply_token, message->peer, &response);
    } else if (message->kind == NB_MSG_KIND_RESPONSE) {
        peer_record(ctx, message, &ctx->responses);
    } else if (message->kind == NB_MSG_KIND_EVENT) {
        peer_record(ctx, message, &ctx->events);
    }
}

static int peer_count(peer_ctx_t *ctx, int *counter)
{
    int value;

    ntk_mutex_lock(ctx->lock);
    value = *counter;
    ntk_mutex_unlock(ctx->lock);
    return value;
}

static nb_transport_instance_t *make_instance(const nb_transport_provider_t *provider,
                                              const char *endpoint,
                                              peer_ctx_t *ctx)
{
    nb_transport_provider_options_t options;
    nb_transport_instance_t *instance = NULL;

    memset(&options, 0, sizeof(options));
    options.scheme = "rtps";
    options.endpoint = endpoint;
    options.user = ctx;
    options.on_receive = on_receive;
    options.intra_process = NB_INTRA_PROCESS_OFF;
    options.struct_size = sizeof(options);
    ctx->provider = provider;
    if (provider->ops->create(&options, &instance) != NB_OK) {
        return NULL;
    }
    ctx->instance = instance;
    return instance;
}

static void test_request_response_and_event(void)
{
    const nb_transport_provider_t *provider = nb_rtps_transport_provider();
    nb_transport_instance_t *a = NULL;
    nb_transport_instance_t *b = NULL;
    peer_ctx_t ctx_a;
    peer_ctx_t ctx_b;
    nb_transport_message_t message;
    static const char ping[] = "ping";
    static const char tick[] = "tick-1";
    int i;

    memset(&ctx_a, 0, sizeof(ctx_a));
    memset(&ctx_b, 0, sizeof(ctx_b));
    ctx_a.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    ctx_b.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    ctx_a.name = "a";
    ctx_b.name = "b";
    CHECK(ctx_a.lock != NULL && ctx_b.lock != NULL);
    if (!ctx_a.lock || !ctx_b.lock) {
        return;
    }

    CHECK(provider != NULL);
    CHECK(nb_rtps_transport_register() == NB_OK);
    CHECK(nb_rtps_transport_register() == NB_OK); /* idempotent */
    if (!provider) {
        goto cleanup;
    }

    a = make_instance(provider, "rtps://7:47130/127.0.0.1:47131", &ctx_a);
    b = make_instance(provider, "rtps://7:47131/127.0.0.1:47130", &ctx_b);
    CHECK(a != NULL && b != NULL);
    if (!a || !b) {
        goto cleanup;
    }
    CHECK(provider->ops->start(a) == NB_OK);
    CHECK(provider->ops->start(b) == NB_OK);
    CHECK(provider->ops->register_method(a, "svc", "echo") == NB_OK);
    CHECK(provider->ops->register_method(b, "svc", "tick") == NB_OK);

    /* Request/response: b asks, a answers through the reply token. */
    memset(&message, 0, sizeof(message));
    message.kind = NB_MSG_KIND_REQUEST;
    message.route = "svc.echo";
    message.request_id = 42;
    message.payload.data = ping;
    message.payload.len = sizeof(ping) - 1u;
    for (i = 0; i < 120 && peer_count(&ctx_b, &ctx_b.responses) == 0; i++) {
        (void)provider->ops->send(b, NULL, &message);
        ntk_sleep_ms(100);
    }
    CHECK(peer_count(&ctx_a, &ctx_a.requests) > 0);
    CHECK(peer_count(&ctx_b, &ctx_b.responses) > 0);
    ntk_mutex_lock(ctx_b.lock);
    CHECK(ctx_b.last_payload_len == 4 && memcmp(ctx_b.last_payload, "pong", 4) == 0);
    CHECK(ctx_b.last_request_id == 42);
    ntk_mutex_unlock(ctx_b.lock);

    /* Event: a publishes one-way, b's event reader receives it. */
    memset(&message, 0, sizeof(message));
    message.kind = NB_MSG_KIND_EVENT;
    message.route = "svc.tick";
    message.payload.data = tick;
    message.payload.len = sizeof(tick) - 1u;
    for (i = 0; i < 120 && peer_count(&ctx_b, &ctx_b.events) == 0; i++) {
        (void)provider->ops->send(a, NULL, &message);
        ntk_sleep_ms(100);
    }
    CHECK(peer_count(&ctx_b, &ctx_b.events) > 0);
    ntk_mutex_lock(ctx_b.lock);
    CHECK(ctx_b.last_payload_len == 6 && memcmp(ctx_b.last_payload, "tick-1", 6) == 0);
    ntk_mutex_unlock(ctx_b.lock);

cleanup:
    if (a) {
        provider->ops->stop(a);
        provider->ops->destroy(a);
    }
    if (b) {
        provider->ops->stop(b);
        provider->ops->destroy(b);
    }
    if (ctx_a.lock) {
        ntk_mutex_destroy(ctx_a.lock);
    }
    if (ctx_b.lock) {
        ntk_mutex_destroy(ctx_b.lock);
    }
}

int main(void)
{
    test_request_response_and_event();
    if (g_failures == 0) {
        nb_log_info("rtps transport tests passed (%d checks)", g_checks);
        return 0;
    }
    nb_log_error("rtps transport tests failed: %d of %d checks", g_failures, g_checks);
    return 1;
}
