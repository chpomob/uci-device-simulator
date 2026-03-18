#include "uci_sim_device.h"

#include <string.h>

static void init_result(uci_sim_result_t* result) {
    memset(result, 0, sizeof(*result));
}

static void make_status_response(const uci_sim_packet_t* request, uci_sim_result_t* result, uint8_t status) {
    result->has_response = 1;
    result->response.mt = UCI_MT_RESPONSE;
    result->response.pbf = UCI_PBF_COMPLETE;
    result->response.gid = request->gid;
    result->response.oid = request->oid;
    result->response.payload_len = 1;
    result->response.payload[0] = status;
}

static uci_sim_session_t* find_session(uci_sim_device_t* device, uint32_t session_id) {
    size_t i;
    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (device->sessions[i].allocated && device->sessions[i].session_id == session_id) {
            return &device->sessions[i];
        }
    }
    return NULL;
}

static uci_sim_session_t* alloc_session(uci_sim_device_t* device, uint32_t session_id) {
    size_t i;
    const uci_sim_profile_t* profile = device && device->profile ? device->profile : uci_sim_default_profile();

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (!device->sessions[i].allocated) {
            device->sessions[i].allocated = 1;
            device->sessions[i].session_id = session_id;
            device->sessions[i].session_type = profile->default_session_type;
            device->sessions[i].state = profile->initial_session_state;
            device->sessions[i].ranging_count = 0;
            device->sessions[i].max_data_size = device->profile
                ? device->profile->default_session_max_data_size
                : 0x0200;
            return &device->sessions[i];
        }
    }
    return NULL;
}

static uint32_t read_u32_le(const uint8_t* payload) {
    return (uint32_t)payload[0] |
           ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) |
           ((uint32_t)payload[3] << 24);
}

static uint16_t read_u16_le(const uint8_t* payload) {
    return (uint16_t)payload[0] |
           (uint16_t)((uint16_t)payload[1] << 8);
}

static void write_u32_le(uint8_t* payload, uint32_t value) {
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)((value >> 8) & 0xFFU);
    payload[2] = (uint8_t)((value >> 16) & 0xFFU);
    payload[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void write_u64_le(uint8_t* payload, uint64_t value) {
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)((value >> 8) & 0xFFU);
    payload[2] = (uint8_t)((value >> 16) & 0xFFU);
    payload[3] = (uint8_t)((value >> 24) & 0xFFU);
    payload[4] = (uint8_t)((value >> 32) & 0xFFU);
    payload[5] = (uint8_t)((value >> 40) & 0xFFU);
    payload[6] = (uint8_t)((value >> 48) & 0xFFU);
    payload[7] = (uint8_t)((value >> 56) & 0xFFU);
}

static void emit_session_status_ntf(uci_sim_device_t* device,
                                    uint32_t session_id,
                                    uint8_t state,
                                    uci_sim_result_t* result) {
    uci_sim_packet_t notification;
    const uci_sim_profile_t* profile = device && device->profile ? device->profile : uci_sim_default_profile();

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONFIG;
    notification.oid = UCI_SESSION_STATUS_NTF;
    notification.payload_len = 6;
    write_u32_le(notification.payload, session_id);
    notification.payload[4] = state;
    notification.payload[5] = profile->session_status_reason_code;

    (void)uci_sim_device_deliver_notification(device, &notification, result);
}

static void emit_device_status_ntf(uci_sim_device_t* device,
                                   uint8_t state,
                                   uci_sim_result_t* result) {
    uci_sim_packet_t notification;

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_CORE;
    notification.oid = UCI_CORE_DEVICE_STATUS_NTF;
    notification.payload_len = 1;
    notification.payload[0] = state;

    (void)uci_sim_device_deliver_notification(device, &notification, result);
}

static int handle_session_set_get_config(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result,
                                         int is_set);
static int handle_core_set_get_config(uci_sim_device_t* device,
                                      const uci_sim_packet_t* request,
                                      uci_sim_result_t* result,
                                      int is_set);
static int handle_session_update_multicast_list(uci_sim_device_t* device,
                                                const uci_sim_packet_t* request,
                                                uci_sim_result_t* result);

static int handle_core(uci_sim_device_t* device, const uci_sim_packet_t* request, uci_sim_result_t* result) {
    if (!uci_sim_profile_supports_command(device->profile, request->gid, request->oid)) {
        make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
        return -1;
    }

    switch (request->oid) {
        case UCI_CORE_DEVICE_RESET:
            if (request->payload_len != 1) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }
            uci_sim_device_reset_runtime_state(device);
            make_status_response(request, result, UCI_STATUS_OK);
            emit_device_status_ntf(device, device->device_state, result);
            return 0;
        case UCI_CORE_DEVICE_INFO:
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_CORE;
            result->response.oid = UCI_CORE_DEVICE_INFO;
            result->response.payload_len = 10;
            result->response.payload[0] = UCI_STATUS_OK;
            result->response.payload[1] = (uint8_t)(device->uci_version & 0xFFU);
            result->response.payload[2] = (uint8_t)((device->uci_version >> 8) & 0xFFU);
            result->response.payload[3] = (uint8_t)(device->mac_version & 0xFFU);
            result->response.payload[4] = (uint8_t)((device->mac_version >> 8) & 0xFFU);
            result->response.payload[5] = (uint8_t)(device->phy_version & 0xFFU);
            result->response.payload[6] = (uint8_t)((device->phy_version >> 8) & 0xFFU);
            result->response.payload[7] = (uint8_t)(device->test_version & 0xFFU);
            result->response.payload[8] = (uint8_t)((device->test_version >> 8) & 0xFFU);
            result->response.payload[9] = 0x00;
            return 0;
        case UCI_CORE_GET_CAPS_INFO:
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_CORE;
            result->response.oid = UCI_CORE_GET_CAPS_INFO;
            if (!device->profile || device->profile->core_caps_payload_len == 0) {
                make_status_response(request, result, UCI_STATUS_FAILED);
                return -1;
            }
            result->response.payload_len = device->profile->core_caps_payload_len;
            memcpy(result->response.payload,
                   device->profile->core_caps_payload,
                   device->profile->core_caps_payload_len);
            return 0;
        case UCI_CORE_SET_CONFIG:
            return handle_core_set_get_config(device, request, result, 1);
        case UCI_CORE_GET_CONFIG:
            return handle_core_set_get_config(device, request, result, 0);
        case UCI_CORE_QUERY_UWBS_TIMESTAMP:
            if (request->payload_len != 0) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_CORE;
            result->response.oid = UCI_CORE_QUERY_UWBS_TIMESTAMP;
            result->response.payload_len = 9;
            result->response.payload[0] = UCI_STATUS_OK;
            write_u64_le(&result->response.payload[1], device->next_uwbs_timestamp);
            device->next_uwbs_timestamp += device->profile
                ? device->profile->uwbs_timestamp_increment
                : 1ULL;
            return 0;
        default:
            make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
            return -1;
    }
}

static int handle_core_set_get_config(uci_sim_device_t* device,
                                      const uci_sim_packet_t* request,
                                      uci_sim_result_t* result,
                                      int is_set) {
    uint8_t count;
    size_t offset;
    uint8_t processed = 0;

    if (request->payload_len < 1) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    count = request->payload[0];
    result->has_response = 1;
    result->response.mt = UCI_MT_RESPONSE;
    result->response.pbf = UCI_PBF_COMPLETE;
    result->response.gid = UCI_GID_CORE;
    result->response.oid = request->oid;
    result->response.payload[0] = UCI_STATUS_OK;
    result->response.payload[1] = 0;
    offset = 1;

    if (is_set) {
        size_t response_offset = 2;

        while (processed < count && offset + 2 <= request->payload_len) {
            uint8_t config_id = request->payload[offset++];
            uint8_t value_len = request->payload[offset++];

            if (offset + value_len > request->payload_len || response_offset + 2 > UCI_SIM_MAX_PAYLOAD) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (!uci_sim_profile_supports_core_config(device->profile, config_id)) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (uci_sim_device_store_config(device, config_id, &request->payload[offset], value_len) != 0) {
                result->response.payload[0] = UCI_STATUS_FAILED;
                break;
            }

            result->response.payload[response_offset++] = config_id;
            result->response.payload[response_offset++] = UCI_STATUS_OK;
            offset += value_len;
            processed++;
        }

        if (processed != count && result->response.payload[0] == UCI_STATUS_OK) {
            result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
        }

        result->response.payload[1] = processed;
        result->response.payload_len = (uint16_t)(2 + (processed * 2));
        return (result->response.payload[0] == UCI_STATUS_OK) ? 0 : -1;
    }

    {
        size_t response_offset = 2;

        while (processed < count && offset < request->payload_len) {
            uint8_t config_id = request->payload[offset++];
            uint8_t value_len = 0;
            uint8_t value[UCI_SIM_MAX_CONFIG_VALUE] = {0};

            if (!uci_sim_profile_supports_core_config(device->profile, config_id)) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (uci_sim_device_get_config(device, config_id, value, &value_len) != 0) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (response_offset + 2 + value_len > UCI_SIM_MAX_PAYLOAD) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }

            result->response.payload[response_offset++] = config_id;
            result->response.payload[response_offset++] = value_len;
            if (value_len > 0) {
                memcpy(&result->response.payload[response_offset], value, value_len);
                response_offset += value_len;
            }
            processed++;
        }

        if (processed == 0) {
            result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
        }

        result->response.payload[1] = processed;
        result->response.payload_len = (uint16_t)response_offset;
    }

    return (result->response.payload[0] == UCI_STATUS_OK) ? 0 : -1;
}

static int handle_session_config(uci_sim_device_t* device, const uci_sim_packet_t* request, uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;
    size_t i;

    if (!uci_sim_profile_supports_command(device->profile, request->gid, request->oid)) {
        make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
        return -1;
    }

    switch (request->oid) {
        case UCI_SESSION_INIT:
            if (request->payload_len < 5) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }
            session_id = read_u32_le(request->payload);
            session = find_session(device, session_id);
            if (session == NULL) {
                session = alloc_session(device, session_id);
            }
            if (session == NULL) {
                make_status_response(request, result, UCI_STATUS_REJECTED);
                return -1;
            }
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONFIG;
            result->response.oid = UCI_SESSION_INIT;
            result->response.payload_len = 5;
            result->response.payload[0] = UCI_STATUS_OK;
            write_u32_le(&result->response.payload[1], session_id);
            emit_session_status_ntf(device, session_id, session->state, result);
            return 0;
        case UCI_SESSION_DEINIT:
            if (request->payload_len < 4) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }
            session_id = read_u32_le(request->payload);
            session = find_session(device, session_id);
            if (session == NULL) {
                make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
                return -1;
            }
            memset(session, 0, sizeof(*session));
            make_status_response(request, result, UCI_STATUS_OK);
            return 0;
        case UCI_SESSION_SET_APP_CONFIG:
            return handle_session_set_get_config(device, request, result, 1);
        case UCI_SESSION_GET_APP_CONFIG:
            return handle_session_set_get_config(device, request, result, 0);
        case UCI_SESSION_GET_COUNT:
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONFIG;
            result->response.oid = UCI_SESSION_GET_COUNT;
            result->response.payload_len = 2;
            result->response.payload[0] = UCI_STATUS_OK;
            result->response.payload[1] = 0;
            for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
                if (device->sessions[i].allocated) {
                    result->response.payload[1]++;
                }
            }
            return 0;
        case UCI_SESSION_QUERY_DATA_SIZE_IN_RANGING:
            if (request->payload_len < 4) {
                result->has_response = 1;
                result->response.mt = UCI_MT_RESPONSE;
                result->response.pbf = UCI_PBF_COMPLETE;
                result->response.gid = UCI_GID_SESSION_CONFIG;
                result->response.oid = UCI_SESSION_QUERY_DATA_SIZE_IN_RANGING;
                result->response.payload_len = 3;
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                result->response.payload[1] = 0x00;
                result->response.payload[2] = 0x00;
                return -1;
            }
            session_id = read_u32_le(request->payload);
            session = find_session(device, session_id);
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONFIG;
            result->response.oid = UCI_SESSION_QUERY_DATA_SIZE_IN_RANGING;
            result->response.payload_len = 3;
            result->response.payload[0] = (session != NULL) ? UCI_STATUS_OK : UCI_STATUS_INVALID_PARAM;
            if (session != NULL) {
                result->response.payload[1] = (uint8_t)(session->max_data_size & 0xFFU);
                result->response.payload[2] = (uint8_t)((session->max_data_size >> 8) & 0xFFU);
                return 0;
            }
            result->response.payload[1] = 0x00;
            result->response.payload[2] = 0x00;
            return -1;
        case UCI_SESSION_GET_STATE:
            if (request->payload_len < 4) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }
            session_id = read_u32_le(request->payload);
            session = find_session(device, session_id);
            if (session == NULL) {
                make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
                return -1;
            }
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONFIG;
            result->response.oid = UCI_SESSION_GET_STATE;
            result->response.payload_len = 2;
            result->response.payload[0] = UCI_STATUS_OK;
            result->response.payload[1] = session->state;
            return 0;
        case UCI_SESSION_UPDATE_CONTROLLER_MULTICAST_LIST:
            return handle_session_update_multicast_list(device, request, result);
        default:
            make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
            return -1;
    }
}

static int handle_session_update_multicast_list(uci_sim_device_t* device,
                                                const uci_sim_packet_t* request,
                                                uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;
    uint8_t entry_count;
    uint8_t action;
    size_t offset;
    size_t response_offset = 2;
    uint8_t processed = 0;
    uint8_t overall_status = UCI_STATUS_OK;

    if (request->payload_len < 6) {
        make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
        return -1;
    }

    session_id = read_u32_le(request->payload);
    entry_count = request->payload[4];
    action = request->payload[5];
    session = find_session(device, session_id);
    if (session == NULL) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }
    if (!uci_sim_profile_supports_multicast_action(device->profile, action)) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    result->has_response = 1;
    result->response.mt = UCI_MT_RESPONSE;
    result->response.pbf = UCI_PBF_COMPLETE;
    result->response.gid = UCI_GID_SESSION_CONFIG;
    result->response.oid = UCI_SESSION_UPDATE_CONTROLLER_MULTICAST_LIST;
    result->response.payload[0] = UCI_STATUS_OK;
    result->response.payload[1] = 0;
    offset = 6;

    while (processed < entry_count) {
        uint16_t short_address;
        uint32_t subsession_id;
        const uint8_t* key = NULL;
        uint8_t key_len = 0;
        int entry_status;

        if (offset + 6 > request->payload_len || response_offset + 7 > UCI_SIM_MAX_PAYLOAD) {
            overall_status = UCI_STATUS_INVALID_PARAM;
            break;
        }

        short_address = read_u16_le(&request->payload[offset]);
        offset += 2;
        subsession_id = read_u32_le(&request->payload[offset]);
        offset += 4;

        if (action == UCI_MULTICAST_ACTION_ADD_SHORT_KEY) {
            key_len = 16;
        } else if (action == UCI_MULTICAST_ACTION_ADD_LONG_KEY) {
            key_len = 32;
        }
        if (key_len > 0) {
            if (offset + key_len <= request->payload_len) {
                key = &request->payload[offset];
                offset += key_len;
            } else if (offset != request->payload_len) {
                overall_status = UCI_STATUS_INVALID_PARAM;
                break;
            } else {
                key_len = 0;
            }
        }

        if (action == UCI_MULTICAST_ACTION_REMOVE) {
            entry_status = uci_sim_session_remove_multicast_entry(session, short_address, subsession_id);
        } else {
            entry_status = uci_sim_session_add_multicast_entry(session, short_address, subsession_id, key, key_len);
        }

        result->response.payload[response_offset++] = (uint8_t)(short_address & 0xFFU);
        result->response.payload[response_offset++] = (uint8_t)((short_address >> 8) & 0xFFU);
        write_u32_le(&result->response.payload[response_offset], subsession_id);
        response_offset += 4;
        result->response.payload[response_offset++] = (uint8_t)entry_status;
        processed++;

        if (entry_status != UCI_STATUS_OK && overall_status == UCI_STATUS_OK) {
            overall_status = UCI_STATUS_FAILED;
        }
    }

    if (processed != entry_count && overall_status == UCI_STATUS_OK) {
        overall_status = UCI_STATUS_INVALID_PARAM;
    }

    result->response.payload[0] = overall_status;
    result->response.payload[1] = processed;
    result->response.payload_len = (uint16_t)response_offset;
    return (overall_status == UCI_STATUS_OK) ? 0 : -1;
}

static int handle_session_set_get_config(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result,
                                         int is_set) {
    uint32_t session_id;
    uci_sim_session_t* session;
    uint8_t count;
    size_t offset;
    uint8_t processed = 0;

    if (request->payload_len < 5) {
        make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
        return -1;
    }

    session_id = read_u32_le(request->payload);
    count = request->payload[4];
    session = find_session(device, session_id);
    if (session == NULL) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    result->has_response = 1;
    result->response.mt = UCI_MT_RESPONSE;
    result->response.pbf = UCI_PBF_COMPLETE;
    result->response.gid = UCI_GID_SESSION_CONFIG;
    result->response.oid = request->oid;
    result->response.payload[0] = UCI_STATUS_OK;
    result->response.payload[1] = 0;
    offset = 5;

    if (is_set) {
        size_t response_offset = 2;
        while (processed < count && offset + 2 <= request->payload_len) {
            uint8_t config_id = request->payload[offset++];
            uint8_t value_len = request->payload[offset++];
            if (offset + value_len > request->payload_len || response_offset + 2 > UCI_SIM_MAX_PAYLOAD) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (!uci_sim_profile_supports_session_app_config(device->profile, config_id)) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (uci_sim_session_store_config(session, config_id, &request->payload[offset], value_len) != 0) {
                result->response.payload[0] = UCI_STATUS_REJECTED;
                break;
            }
            result->response.payload[response_offset++] = config_id;
            result->response.payload[response_offset++] = UCI_STATUS_OK;
            offset += value_len;
            processed++;
        }
        result->response.payload[1] = processed;
        result->response.payload_len = (uint16_t)(2 + (processed * 2));
        return (result->response.payload[0] == UCI_STATUS_OK) ? 0 : -1;
    }

    {
        size_t response_offset = 2;
        while (processed < count && offset < request->payload_len) {
            uint8_t config_id = request->payload[offset++];
            uint8_t value_len = 0;
            uint8_t value[UCI_SIM_MAX_CONFIG_VALUE] = {0};
            if (!uci_sim_profile_supports_session_app_config(device->profile, config_id)) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (uci_sim_session_get_config(session, config_id, value, &value_len) != 0) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (response_offset + 2 + value_len > UCI_SIM_MAX_PAYLOAD) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            result->response.payload[response_offset++] = config_id;
            result->response.payload[response_offset++] = value_len;
            if (value_len > 0) {
                memcpy(&result->response.payload[response_offset], value, value_len);
                response_offset += value_len;
            }
            processed++;
        }
        result->response.payload[1] = processed;
        result->response.payload_len = (uint16_t)response_offset;
    }

    return (result->response.payload[0] == UCI_STATUS_OK) ? 0 : -1;
}

static int handle_session_control(uci_sim_device_t* device, const uci_sim_packet_t* request, uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;
    const uci_sim_session_transition_t* transition;

    if (!uci_sim_profile_supports_command(device->profile, request->gid, request->oid)) {
        make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
        return -1;
    }

    if (request->payload_len < 4) {
        make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
        return -1;
    }

    session_id = read_u32_le(request->payload);
    session = find_session(device, session_id);
    if (session == NULL) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    switch (request->oid) {
        case UCI_SESSION_GET_RANGING_COUNT:
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONTROL;
            result->response.oid = UCI_SESSION_GET_RANGING_COUNT;
            result->response.payload_len = 5;
            result->response.payload[0] = UCI_STATUS_OK;
            write_u32_le(&result->response.payload[1], session->ranging_count);
            return 0;
        default:
            transition = uci_sim_profile_get_session_transition(device->profile, request->oid);
            if (transition == NULL) {
                make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
                return -1;
            }
            break;
    }

    transition = uci_sim_profile_get_session_transition(device->profile, request->oid);
    if (transition == NULL) {
        make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
        return -1;
    }
    if ((transition->allowed_states_mask & (1U << session->state)) == 0U) {
        make_status_response(request, result, transition->invalid_status);
        return -1;
    }

    session->state = transition->next_state;
    make_status_response(request, result, UCI_STATUS_OK);
    emit_session_status_ntf(device, session_id, transition->next_state, result);
    if (request->oid == UCI_SESSION_START) {
        (void)uci_sim_scenario_on_session_started(device, session, result);
    } else if (request->oid == UCI_SESSION_STOP) {
        uci_sim_scenario_on_session_stopped(device, session);
    }
    return 0;
}

int uci_sim_device_handle_packet(uci_sim_device_t* device,
                                 const uci_sim_packet_t* request,
                                 uci_sim_result_t* result) {
    int rc;
    size_t pending_count_before;

    if (!device || !request || !result) {
        return -1;
    }

    init_result(result);
    if (request->mt != UCI_MT_COMMAND) {
        return -1;
    }

    pending_count_before = device->pending_notification_count;

    switch (request->gid) {
        case UCI_GID_CORE:
            rc = handle_core(device, request, result);
            break;
        case UCI_GID_SESSION_CONFIG:
            rc = handle_session_config(device, request, result);
            break;
        case UCI_GID_SESSION_CONTROL:
            rc = handle_session_control(device, request, result);
            break;
        default:
            make_status_response(request, result, UCI_STATUS_UNKNOWN_GID);
            rc = -1;
            break;
    }

    (void)uci_sim_scenario_on_command_complete(device, request, result);
    uci_sim_device_finalize_result(device, result, pending_count_before);
    return rc;
}
