#include "rtps_sedp.h"

#include <string.h>

#include "rtps_param.h"

static const uint8_t *sedp_params_begin(const uint8_t *payload, size_t len, size_t *params_len)
{
    const uint8_t *params = payload;
    size_t remaining = len;

    if (remaining >= 4 && params[0] == 0x00 && params[1] == 0x03 &&
        params[2] == 0x00 && params[3] == 0x00) {
        params += 4;
        remaining -= 4;
    }
    if (params_len) {
        *params_len = remaining;
    }
    return params;
}

int nb_rtps_sedp_build(const nb_rtps_endpoint_info_t *info,
                       void *buf,
                       size_t cap,
                       size_t *out_len)
{
    nb_rtps_param_writer_t writer;
    uint32_t reliability;
    uint32_t history_depth;
    const char *partition[1];
    int rc;

    if (!info || !buf || info->topic_name[0] == '\0' || info->type_name[0] == '\0') {
        return NB_ERR_INVALID;
    }
    if (info->unicast_count > NB_RTPS_SEDP_LOCATORS_MAX ||
        info->multicast_count > NB_RTPS_SEDP_LOCATORS_MAX ||
        info->user_data_len > NB_RTPS_SEDP_USER_DATA_MAX) {
        return NB_ERR_TOOBIG;
    }
    reliability = info->reliability ? info->reliability : NB_RTPS_RELIABILITY_BEST_EFFORT;
    history_depth = info->history_depth ? info->history_depth : 1;

    nb_rtps_param_writer_init(&writer, buf, cap);
    rc = nb_rtps_param_put_encapsulation(&writer);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_guid(&writer, NB_RTPS_PID_ENDPOINT_GUID, &info->endpoint_guid);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_string(&writer, NB_RTPS_PID_TOPIC_NAME, info->topic_name);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_string(&writer, NB_RTPS_PID_TYPE_NAME, info->type_name);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_u32(&writer, NB_RTPS_PID_RELIABILITY, reliability);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_u32(&writer, NB_RTPS_PID_DURABILITY, info->durability);
    if (rc != NB_OK) {
        return rc;
    }
    rc = nb_rtps_param_put_history(
        &writer, NB_RTPS_PID_HISTORY, info->history_kind, history_depth);
    if (rc != NB_OK) {
        return rc;
    }
    if (info->partition[0] != '\0') {
        partition[0] = info->partition;
        rc = nb_rtps_param_put_string_sequence(
            &writer, NB_RTPS_PID_PARTITION, partition, 1);
        if (rc != NB_OK) {
            return rc;
        }
    }
    if (info->unicast_count > 0) {
        rc = nb_rtps_param_put_locator_sequence(
            &writer, NB_RTPS_PID_UNICAST_LOCATOR, info->unicast, info->unicast_count);
        if (rc != NB_OK) {
            return rc;
        }
    }
    if (info->multicast_count > 0) {
        rc = nb_rtps_param_put_locator_sequence(
            &writer, NB_RTPS_PID_MULTICAST_LOCATOR, info->multicast, info->multicast_count);
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

int nb_rtps_sedp_parse(const void *payload, size_t len, nb_rtps_endpoint_info_t *out)
{
    nb_rtps_param_reader_t reader;
    const uint8_t *params;
    size_t params_len = 0;
    uint16_t pid = 0;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    int seen_guid = 0;
    int seen_topic = 0;
    int seen_type = 0;
    int rc;

    if (!payload || !out) {
        return NB_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->reliability = NB_RTPS_RELIABILITY_BEST_EFFORT;
    out->durability = NB_RTPS_DURABILITY_VOLATILE;
    out->history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    out->history_depth = 1;

    params = sedp_params_begin((const uint8_t *)payload, len, &params_len);
    nb_rtps_param_reader_init(&reader, params, params_len);
    for (;;) {
        rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
        if (rc < 0) {
            return rc;
        }
        if (rc == 0) {
            break;
        }
        switch (pid) {
        case NB_RTPS_PID_ENDPOINT_GUID:
            rc = nb_rtps_param_get_guid(value, value_len, &out->endpoint_guid);
            if (rc != NB_OK) {
                return rc;
            }
            seen_guid = 1;
            break;
        case NB_RTPS_PID_TOPIC_NAME:
            rc = nb_rtps_param_get_string(
                value, value_len, out->topic_name, sizeof(out->topic_name));
            if (rc != NB_OK) {
                return rc;
            }
            seen_topic = 1;
            break;
        case NB_RTPS_PID_TYPE_NAME:
            rc = nb_rtps_param_get_string(
                value, value_len, out->type_name, sizeof(out->type_name));
            if (rc != NB_OK) {
                return rc;
            }
            seen_type = 1;
            break;
        case NB_RTPS_PID_RELIABILITY:
            rc = nb_rtps_param_get_u32(value, value_len, &out->reliability);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_DURABILITY:
            rc = nb_rtps_param_get_u32(value, value_len, &out->durability);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_HISTORY:
            rc = nb_rtps_param_get_history(
                value, value_len, &out->history_kind, &out->history_depth);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_PARTITION:
            rc = nb_rtps_param_get_first_string(
                value, value_len, out->partition, sizeof(out->partition));
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_UNICAST_LOCATOR:
            rc = nb_rtps_param_get_locator_sequence(value,
                                                    value_len,
                                                    out->unicast,
                                                    NB_RTPS_SEDP_LOCATORS_MAX,
                                                    &out->unicast_count);
            if (rc != NB_OK) {
                return rc;
            }
            break;
        case NB_RTPS_PID_MULTICAST_LOCATOR:
            rc = nb_rtps_param_get_locator_sequence(value,
                                                    value_len,
                                                    out->multicast,
                                                    NB_RTPS_SEDP_LOCATORS_MAX,
                                                    &out->multicast_count);
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
    if (!seen_guid || !seen_topic || !seen_type ||
        out->topic_name[0] == '\0' || out->type_name[0] == '\0') {
        return NB_ERR_BAD_MESSAGE;
    }
    return NB_OK;
}
