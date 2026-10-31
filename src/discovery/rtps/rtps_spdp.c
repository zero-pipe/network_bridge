#include "rtps_spdp.h"

#include <string.h>

#include "rtps_param.h"

int nb_rtps_spdp_build(const nb_rtps_participant_info_t *info,
                       void *buf,
                       size_t cap,
                       size_t *out_len)
{
    nb_rtps_param_writer_t writer;
    int rc;

    if (!info || !buf) {
        return NB_ERR_INVALID;
    }
    nb_rtps_param_writer_init(&writer, buf, cap);
    rc = nb_rtps_param_put_octets2(&writer,
                                   NB_RTPS_PID_PROTOCOL_VERSION,
                                   NB_RTPS_PROTOCOL_VERSION_MAJOR,
                                   NB_RTPS_PROTOCOL_VERSION_MINOR);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_octets2(&writer,
                                   NB_RTPS_PID_VENDORID,
                                   info->vendor_id[0],
                                   info->vendor_id[1]);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_guid(&writer, NB_RTPS_PID_PARTICIPANT_GUID, &info->guid);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_duration(
        &writer, NB_RTPS_PID_PARTICIPANT_LEASE_DURATION, &info->lease_duration);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_u32(
        &writer, NB_RTPS_PID_BUILTIN_ENDPOINT_SET, info->builtin_endpoints);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_locator(
        &writer, NB_RTPS_PID_METATRAFFIC_UNICAST_LOCATOR, &info->metatraffic_unicast);
    if (rc != NB_OK) {
        return rc;
    }
    if (info->metatraffic_multicast.kind != NB_RTPS_LOCATOR_KIND_INVALID) {
        rc = nb_rtps_param_put_locator(
            &writer, NB_RTPS_PID_METATRAFFIC_MULTICAST_LOCATOR, &info->metatraffic_multicast);
        if (rc != NB_OK) {
            return rc;
        }
    }
    if (info->default_unicast.kind != NB_RTPS_LOCATOR_KIND_INVALID) {
        rc = nb_rtps_param_put_locator(
            &writer, NB_RTPS_PID_DEFAULT_UNICAST_LOCATOR, &info->default_unicast);
        if (rc != NB_OK) {
            return rc;
        }
    }
    if (info->default_multicast.kind != NB_RTPS_LOCATOR_KIND_INVALID) {
        rc = nb_rtps_param_put_locator(
            &writer, NB_RTPS_PID_DEFAULT_MULTICAST_LOCATOR, &info->default_multicast);
        if (rc != NB_OK) {
            return rc;
        }
    }
    if (info->name[0] != '\0') {
        rc = nb_rtps_param_put_string(&writer, NB_RTPS_PID_ENTITY_NAME, info->name);
        if (rc != NB_OK) {
            return rc;
        }
    }
    if (info->user_data_len > 0) {
        rc = nb_rtps_param_put_octet_sequence(
            &writer, NB_RTPS_PID_USER_DATA, info->user_data, info->user_data_len);
        if (rc != NB_OK) {
            return rc;
        }
    }
    rc = nb_rtps_param_writer_finish(&writer);
    if (rc != NB_OK) {
        return rc;
    }
    if (out_len) {
        *out_len = nb_rtps_param_writer_len(&writer);
    }
    return NB_OK;
}

int nb_rtps_spdp_parse(const void *payload, size_t len, nb_rtps_participant_info_t *out)
{
    nb_rtps_param_reader_t reader;
    uint16_t pid = 0;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    uint8_t version_major = 0;
    int seen_version = 0;
    int seen_guid = 0;
    int seen_lease = 0;
    int seen_endpoints = 0;
    int seen_metatraffic = 0;
    int rc;

    if (!payload || !out) {
        return NB_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    nb_rtps_param_reader_init(&reader, payload, len);
    for (;;) {
        rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
        if (rc < 0) {
            return rc;
        }
        if (rc == 0) {
            break;
        }
        switch (pid) {
        case NB_RTPS_PID_PROTOCOL_VERSION: {
            uint8_t minor = 0;
            rc = nb_rtps_param_get_octets2(value, value_len, &version_major, &minor);
            if (rc != NB_OK || version_major != NB_RTPS_PROTOCOL_VERSION_MAJOR) {
                return NB_ERR_NOT_SUPPORTED;
            }
            seen_version = 1;
            break;
        }
        case NB_RTPS_PID_VENDORID:
            rc = nb_rtps_param_get_octets2(value,
                                           value_len,
                                           &out->vendor_id[0],
                                           &out->vendor_id[1]);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_PARTICIPANT_GUID:
            rc = nb_rtps_param_get_guid(value, value_len, &out->guid);
            if (rc != NB_OK) {
                return rc;
            }
            seen_guid = 1;
            break;
        case NB_RTPS_PID_PARTICIPANT_LEASE_DURATION:
            rc = nb_rtps_param_get_duration(value, value_len, &out->lease_duration);
            if (rc != NB_OK) {
                return rc;
            }
            seen_lease = 1;
            break;
        case NB_RTPS_PID_BUILTIN_ENDPOINT_SET:
            rc = nb_rtps_param_get_u32(value, value_len, &out->builtin_endpoints);
            if (rc != NB_OK) {
                return rc;
            }
            seen_endpoints = 1;
            break;
        case NB_RTPS_PID_METATRAFFIC_UNICAST_LOCATOR:
            rc = nb_rtps_param_get_locator(value, value_len, &out->metatraffic_unicast);
            if (rc != NB_OK) {
                return rc;
            }
            seen_metatraffic = 1;
            break;
        case NB_RTPS_PID_METATRAFFIC_MULTICAST_LOCATOR:
            rc = nb_rtps_param_get_locator(value, value_len, &out->metatraffic_multicast);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_DEFAULT_UNICAST_LOCATOR:
            rc = nb_rtps_param_get_locator(value, value_len, &out->default_unicast);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_DEFAULT_MULTICAST_LOCATOR:
            rc = nb_rtps_param_get_locator(value, value_len, &out->default_multicast);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_ENTITY_NAME:
            rc = nb_rtps_param_get_string(
                value, value_len, out->name, sizeof(out->name));
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_USER_DATA: {
            const uint8_t *data = NULL;
            size_t data_len = 0;
            rc = nb_rtps_param_get_octet_sequence(value, value_len, &data, &data_len);
            if (rc != NB_OK) {
                return rc;
            }
            if (data_len > sizeof(out->user_data)) {
                return NB_ERR_TOOBIG;
            }
            if (data_len > 0) {
                memcpy(out->user_data, data, data_len);
            }
            out->user_data_len = data_len;
            break;
        }
        default:
            /* Unknown and vendor-specific parameters are ignored. */
            break;
        }
    }
    if (!nb_rtps_param_reader_ok(&reader)) {
        return NB_ERR_BAD_MESSAGE;
    }
    if (!seen_version || !seen_guid || !seen_lease || !seen_endpoints || !seen_metatraffic) {
        return NB_ERR_BAD_MESSAGE;
    }
    return NB_OK;
}
