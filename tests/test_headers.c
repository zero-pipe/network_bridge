/* Compile-time smoke test for the role-based public headers. */
#include <nb.h>
#include <nb/runtime.h>
#include <nb/server.h>
#include <nb/client.h>
#include <nb/transport.h>
#include <nb/extensions.h>
#include <nb/config.h>

/* Legacy flat paths must keep compiling as forwarding headers. */
#include <types.h>
#include <service.h>
#include <proxy.h>
#include <config.h>
#include <dds.h>

int main(void)
{
    nb_payload_t payload = { 0 };
    nb_endpoint_t endpoint = { 0 };
    nb_message_view_t view;

    nb_message_view_init(&view);
    nb_message_view_to_payload(&view, &payload);
    (void)endpoint;
    return payload.data == NULL && payload.len == 0 ? 0 : 1;
}
