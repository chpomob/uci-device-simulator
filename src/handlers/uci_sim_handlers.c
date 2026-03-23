#include "uci_sim_device.h"
#include "uci_sim_validation.h"

#include <string.h>

static void emit_session_status_ntf_with_reason(uci_sim_device_t* device,
                                                uint32_t session_id,
                                                uint8_t state,
                                                uint8_t reason,
                                                uci_sim_result_t* result);

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
    size_t config_index;
    const uci_sim_profile_t* profile = device && device->profile ? device->profile : uci_sim_default_profile();

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (!device->sessions[i].allocated) {
            device->sessions[i].allocated = 1;
            device->sessions[i].session_id = session_id;
            device->sessions[i].session_type = profile->default_session_type;
            device->sessions[i].state = profile->initial_session_state;
            device->sessions[i].ranging_count = 0;
            device->sessions[i].has_last_proximity_state = 0;
            device->sessions[i].last_in_proximity_range = 0;
            device->sessions[i].max_data_size = device->profile
                ? device->profile->default_session_max_data_size
                : 0x0200;
            for (config_index = 0; config_index < profile->default_session_app_config_count; ++config_index) {
                (void)uci_sim_session_store_config(&device->sessions[i],
                                                   profile->default_session_app_config_ids[config_index],
                                                   profile->default_session_app_config_values[config_index],
                                                   profile->default_session_app_config_value_lens[config_index]);
            }
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

static void write_u16_le(uint8_t* payload, uint16_t value) {
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void emit_session_status_ntf(uci_sim_device_t* device,
                                    uint32_t session_id,
                                    uint8_t state,
                                    uci_sim_result_t* result) {
    const uci_sim_profile_t* profile = device && device->profile ? device->profile : uci_sim_default_profile();

    emit_session_status_ntf_with_reason(device,
                                        session_id,
                                        state,
                                        profile->session_status_reason_code,
                                        result);
}

static void emit_session_status_ntf_with_reason(uci_sim_device_t* device,
                                                uint32_t session_id,
                                                uint8_t state,
                                                uint8_t reason,
                                                uci_sim_result_t* result) {
    uci_sim_packet_t notification;

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONFIG;
    notification.oid = UCI_SESSION_STATUS_NTF;
    notification.payload_len = 6;
    write_u32_le(notification.payload, session_id);
    notification.payload[4] = state;
    notification.payload[5] = reason;

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

static void emit_core_generic_error_ntf(uci_sim_device_t* device,
                                        uint8_t status,
                                        uci_sim_result_t* result) {
    uci_sim_packet_t notification;

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_CORE;
    notification.oid = UCI_CORE_GENERIC_ERROR;
    notification.payload_len = 1;
    notification.payload[0] = status;

    (void)uci_sim_device_deliver_notification(device, &notification, result);
}

static void emit_logical_link_notification(uci_sim_device_t* device,
                                           uci_sim_result_t* result,
                                           uint32_t session_id,
                                           uint8_t oid,
                                           uint8_t link_id,
                                           uint8_t reason_or_credit) {
    uci_sim_packet_t notification;

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONTROL;
    notification.oid = oid;
    notification.payload_len = 6;
    write_u32_le(notification.payload, session_id);
    notification.payload[4] = link_id;
    notification.payload[5] = reason_or_credit;

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
static int handle_session_update_dt_rounds(uci_sim_device_t* device,
                                           const uci_sim_packet_t* request,
                                           uci_sim_result_t* result,
                                           int use_anchor_rounds);
static int handle_session_set_hus_config(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result,
                                         int use_controller_config);
static int handle_session_data_transfer_phase_config(uci_sim_device_t* device,
                                                     const uci_sim_packet_t* request,
                                                     uci_sim_result_t* result);
static int handle_data_message_send(uci_sim_device_t* device,
                                    const uci_sim_packet_t* request,
                                    uci_sim_result_t* result);
static void emit_logical_link_notification(uci_sim_device_t* device,
                                           uci_sim_result_t* result,
                                           uint32_t session_id,
                                           uint8_t oid,
                                           uint8_t link_id,
                                           uint8_t reason_or_credit);

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
        case UCI_SESSION_UPDATE_DT_ANCHOR_RANGING_ROUNDS:
            return handle_session_update_dt_rounds(device, request, result, 1);
        case UCI_SESSION_UPDATE_DT_TAG_RANGING_ROUNDS:
            return handle_session_update_dt_rounds(device, request, result, 0);
        case UCI_SESSION_SET_HUS_CONTROLLER_CONFIG:
            return handle_session_set_hus_config(device, request, result, 1);
        case UCI_SESSION_SET_HUS_CONTROLEE_CONFIG:
            return handle_session_set_hus_config(device, request, result, 0);
        case UCI_SESSION_DATA_TRANSFER_PHASE_CONFIG:
            return handle_session_data_transfer_phase_config(device, request, result);
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

static int handle_session_data_transfer_phase_config(uci_sim_device_t* device,
                                                     const uci_sim_packet_t* request,
                                                     uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;
    uint8_t dtp_size;
    uint8_t payload_len;

    if (request->payload_len < 7) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    session_id = read_u32_le(request->payload);
    session = find_session(device, session_id);
    if (session == NULL) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    dtp_size = request->payload[6];
    payload_len = (uint8_t)(request->payload_len - 7);
    if (dtp_size > sizeof(session->dtp_payload) || payload_len != dtp_size) {
        make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
        return -1;
    }

    session->dtp_repetition = request->payload[4];
    session->dtp_control = request->payload[5];
    session->dtp_size = dtp_size;
    session->dtp_payload_len = payload_len;
    if (payload_len > 0) {
        memcpy(session->dtp_payload, &request->payload[7], payload_len);
    }
    if (payload_len < sizeof(session->dtp_payload)) {
        memset(&session->dtp_payload[payload_len], 0, sizeof(session->dtp_payload) - payload_len);
    }

    make_status_response(request, result, UCI_STATUS_OK);
    return 0;
}

static int handle_session_set_hus_config(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result,
                                         int use_controller_config) {
    uint32_t session_id;
    uint16_t config_length;
    uci_sim_session_t* session;

    if (request->payload_len < 12) {
        make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
        return -1;
    }

    session_id = read_u32_le(request->payload);
    config_length = read_u16_le(&request->payload[10]);
    if ((size_t)(12 + config_length) != request->payload_len || config_length > 250) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }
    if (request->payload[8] > 1) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    session = find_session(device, session_id);
    if (session == NULL) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    if (use_controller_config) {
        session->hus_controller_primary_session_id = read_u32_le(&request->payload[4]);
        session->hus_controller_role = request->payload[8];
        session->hus_controller_reserved = request->payload[9];
        session->hus_controller_config_length = config_length;
        memset(session->hus_controller_config_data, 0, sizeof(session->hus_controller_config_data));
        if (config_length > 0) {
            memcpy(session->hus_controller_config_data, &request->payload[12], config_length);
        }
    } else {
        session->hus_controlee_primary_session_id = read_u32_le(&request->payload[4]);
        session->hus_controlee_role = request->payload[8];
        session->hus_controlee_reserved = request->payload[9];
        session->hus_controlee_config_length = config_length;
        memset(session->hus_controlee_config_data, 0, sizeof(session->hus_controlee_config_data));
        if (config_length > 0) {
            memcpy(session->hus_controlee_config_data, &request->payload[12], config_length);
        }
    }

    make_status_response(request, result, UCI_STATUS_OK);
    return 0;
}

static int handle_session_update_dt_rounds(uci_sim_device_t* device,
                                           const uci_sim_packet_t* request,
                                           uci_sim_result_t* result,
                                           int use_anchor_rounds) {
    uint32_t session_id;
    uci_sim_session_t* session;
    uint8_t round_count;
    uint8_t* dst_rounds;
    uint8_t* dst_count;

    if (request->payload_len < 5) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    session_id = read_u32_le(request->payload);
    round_count = request->payload[4];
    if (request->payload_len < (size_t)(5 + round_count) || round_count > UCI_SIM_MAX_DT_ROUNDS) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    session = find_session(device, session_id);
    if (session == NULL) {
        make_status_response(request, result, UCI_STATUS_INVALID_PARAM);
        return -1;
    }

    if (use_anchor_rounds) {
        dst_rounds = session->dt_anchor_round_indexes;
        dst_count = &session->dt_anchor_round_count;
    } else {
        dst_rounds = session->dt_tag_round_indexes;
        dst_count = &session->dt_tag_round_count;
    }

    *dst_count = round_count;
    if (round_count > 0) {
        memcpy(dst_rounds, &request->payload[5], round_count);
    }
    if (round_count < UCI_SIM_MAX_DT_ROUNDS) {
        memset(&dst_rounds[round_count], 0, UCI_SIM_MAX_DT_ROUNDS - round_count);
    }

    result->has_response = 1;
    result->response.mt = UCI_MT_RESPONSE;
    result->response.pbf = UCI_PBF_COMPLETE;
    result->response.gid = UCI_GID_SESSION_CONFIG;
    result->response.oid = request->oid;
    result->response.payload_len = (uint16_t)(2 + round_count);
    result->response.payload[0] = UCI_STATUS_OK;
    result->response.payload[1] = round_count;
    if (round_count > 0) {
        memcpy(&result->response.payload[2], dst_rounds, round_count);
    }
    return 0;
}

static int handle_session_set_get_config(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result,
                                         int is_set) {
    uint32_t session_id;
    uci_sim_session_t* session;
    const uci_sim_profile_t* profile;
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
    profile = device && device->profile ? device->profile : uci_sim_default_profile();
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
        while (processed < count && offset + 2 <= request->payload_len) {
            uint8_t config_id = request->payload[offset++];
            uint8_t value_len = request->payload[offset++];
            if (offset + value_len > request->payload_len) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (!uci_sim_profile_supports_session_app_config(device->profile, config_id)) {
                result->response.payload[0] = UCI_STATUS_INVALID_PARAM;
                break;
            }
            if (config_id == UCI_APP_CONFIG_DEVICE_TYPE ||
                config_id == UCI_APP_CONFIG_STS_CONFIG ||
                config_id == UCI_APP_CONFIG_RANGING_INTERVAL ||
                config_id == UCI_APP_CONFIG_RANGING_ROUND_USAGE ||
                config_id == UCI_APP_CONFIG_AOA_RESULT_REQ ||
                config_id == UCI_APP_CONFIG_RSSI_REPORTING ||
                config_id == UCI_APP_CONFIG_RESULT_REPORT_CONFIG) {
                uci_sim_validation_result_t validation;

                if (uci_sim_validate_session_app_config(profile,
                                                        session,
                                                        config_id,
                                                        &request->payload[offset],
                                                        value_len,
                                                        &validation) != 0 &&
                    validation.surface == UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE) {
                    result->response.payload[0] = validation.status;
                    break;
                }
            }
            if (uci_sim_session_store_config(session, config_id, &request->payload[offset], value_len) != 0) {
                result->response.payload[0] = UCI_STATUS_REJECTED;
                break;
            }
            if (config_id == UCI_APP_CONFIG_SESSION_INFO_NTF_CONFIG ||
                config_id == UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_NEAR ||
                config_id == UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_FAR) {
                session->has_last_proximity_state = 0;
                session->last_in_proximity_range = 0;
            }
            if (config_id == UCI_APP_CONFIG_RANGING_INTERVAL &&
                session->state == UCI_SESSION_STATE_ACTIVE &&
                session->ranging_stream_remaining > 0) {
                if (uci_sim_device_reschedule_session_event(device,
                                                            UCI_SIM_EVENT_RANGE_DATA,
                                                            session->session_id,
                                                            uci_sim_session_get_ranging_interval_ms(session, profile)) != 0) {
                    result->response.payload[0] = UCI_STATUS_REJECTED;
                    break;
                }
            }
            offset += value_len;
            processed++;
        }
        result->response.payload[1] = 0;
        result->response.payload_len = 2;
        return (result->response.payload[0] == UCI_STATUS_OK) ? 0 : -1;
    }

    {
        size_t response_offset = 2;
        if (count == 0) {
            size_t i;
            const uci_sim_profile_t* profile = device->profile ? device->profile : uci_sim_default_profile();

            for (i = 0; i < profile->supported_session_app_config_id_count; ++i) {
                uint8_t config_id = profile->supported_session_app_config_ids[i];
                uint8_t value_len = 0;
                uint8_t value[UCI_SIM_MAX_CONFIG_VALUE] = {0};

                if (uci_sim_session_get_config(session, config_id, value, &value_len) != 0) {
                    continue;
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
        }

        while (count > 0 && processed < count && offset < request->payload_len) {
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
    const uci_sim_profile_t* profile;
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
    profile = device && device->profile ? device->profile : uci_sim_default_profile();
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
        case UCI_SESSION_LOGICAL_LINK_CREATE: {
            uint8_t requested_id = (request->payload_len >= 5) ? request->payload[4] : 0xFF;
            uint8_t mode = (request->payload_len >= 6) ? request->payload[5] : 0;
            uint8_t credit = (request->payload_len >= 7) ? request->payload[6] : ((request->payload_len >= 6) ? 1 : 1);
            uint8_t assigned_id = 0xFF;
            uci_sim_logical_link_t* entry = NULL;
            uint8_t status = UCI_STATUS_OK;

            if (request->payload_len < 5) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }

            if (requested_id != 0xFF && uci_sim_session_find_logical_link(session, requested_id)) {
                status = UCI_STATUS_INVALID_PARAM;
            } else if (session->logical_link_count >= UCI_SIM_MAX_LOGICAL_LINKS) {
                status = UCI_STATUS_MULTICAST_LIST_FULL;
            } else {
                entry = uci_sim_session_allocate_logical_link(session, requested_id, &assigned_id);
                if (!entry) {
                    status = UCI_STATUS_INVALID_PARAM;
                } else {
                    entry->mode = mode;
                    entry->credit = credit;
                }
            }

            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONTROL;
            result->response.oid = UCI_SESSION_LOGICAL_LINK_CREATE;
            result->response.payload_len = 3;
            result->response.payload[0] = status;
            result->response.payload[1] = (status == UCI_STATUS_OK) ? assigned_id : 0xFF;
            result->response.payload[2] = (status == UCI_STATUS_OK && entry) ? entry->credit : 0;
            if (status == UCI_STATUS_OK && entry) {
                emit_logical_link_notification(device,
                                               result,
                                               session_id,
                                               UCI_SESSION_LOGICAL_LINK_UWBS_CREATE,
                                               assigned_id,
                                               entry->credit);
                return 0;
            }
            return -1;
        }
        case UCI_SESSION_LOGICAL_LINK_CLOSE: {
            uint8_t link_id;
            uint8_t status = UCI_STATUS_OK;

            if (request->payload_len < 5) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }

            link_id = request->payload[4];
            if (uci_sim_session_remove_logical_link(session, link_id) != 0) {
                status = UCI_STATUS_INVALID_PARAM;
            }

            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONTROL;
            result->response.oid = UCI_SESSION_LOGICAL_LINK_CLOSE;
            result->response.payload_len = 2;
            result->response.payload[0] = status;
            result->response.payload[1] = link_id;
            if (status == UCI_STATUS_OK) {
                emit_logical_link_notification(device,
                                               result,
                                               session_id,
                                               UCI_SESSION_LOGICAL_LINK_UWBS_CLOSE,
                                               link_id,
                                               0x00);
                return 0;
            }
            return -1;
        }
        case UCI_SESSION_LOGICAL_LINK_GET_PARAM: {
            uint8_t link_id;
            uci_sim_logical_link_t* entry;
            uint8_t status;

            if (request->payload_len < 5) {
                make_status_response(request, result, UCI_STATUS_INVALID_MSG_SIZE);
                return -1;
            }

            link_id = request->payload[4];
            entry = uci_sim_session_find_logical_link(session, link_id);
            status = entry ? UCI_STATUS_OK : UCI_STATUS_INVALID_PARAM;

            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_SESSION_CONTROL;
            result->response.oid = UCI_SESSION_LOGICAL_LINK_GET_PARAM;
            result->response.payload_len = 4;
            result->response.payload[0] = status;
            result->response.payload[1] = link_id;
            result->response.payload[2] = (entry && status == UCI_STATUS_OK) ? entry->mode : 0;
            result->response.payload[3] = (entry && status == UCI_STATUS_OK) ? entry->credit : 0;
            return (status == UCI_STATUS_OK) ? 0 : -1;
        }
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

    if (request->oid == UCI_SESSION_START) {
        uci_sim_validation_result_t validation;

        if (uci_sim_validate_session_start(profile, session, &validation) != 0) {
            if (validation.surface == UCI_SIM_INVALID_CONFIG_SURFACE_SESSION_STATUS) {
                make_status_response(request, result, UCI_STATUS_OK);
                emit_session_status_ntf_with_reason(device,
                                                    session_id,
                                                    session->state,
                                                    validation.reason,
                                                    result);
                return 0;
            }
            make_status_response(request, result, validation.status);
            return -1;
        }
    }

    if (request->oid == UCI_SESSION_START || request->oid == UCI_SESSION_STOP) {
        session->has_last_proximity_state = 0;
        session->last_in_proximity_range = 0;
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
    pending_count_before = device->pending_notification_count;

    if (request->mt == UCI_MT_DATA) {
        rc = handle_data_message_send(device, request, result);
        uci_sim_device_finalize_result(device, result, pending_count_before);
        return rc;
    }
    if (request->mt != UCI_MT_COMMAND) {
        return -1;
    }

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

    if (rc != 0 && result->has_response && result->response.payload_len >= 1) {
        emit_core_generic_error_ntf(device, result->response.payload[0], result);
    }

    (void)uci_sim_scenario_on_command_complete(device, request, result);
    uci_sim_device_finalize_result(device, result, pending_count_before);
    return rc;
}

static int handle_data_message_send(uci_sim_device_t* device,
                                    const uci_sim_packet_t* request,
                                    uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;
    uint16_t sequence_number;
    uint16_t declared_length;
    uint8_t transfer_status;
    uint8_t credit_payload[5];
    uint8_t status_payload[8];

    if (!device || !request || !result) {
        return -1;
    }
    if (request->gid != UCI_DATA_PACKET_FORMAT_SEND || request->oid != 0x00) {
        return -1;
    }
    if (request->payload_len < 16) {
        return -1;
    }

    session_id = read_u32_le(request->payload);
    sequence_number = read_u16_le(&request->payload[12]);
    declared_length = read_u16_le(&request->payload[14]);
    session = find_session(device, session_id);

    if ((size_t)(16 + declared_length) > request->payload_len) {
        write_u32_le(status_payload, session_id);
        write_u16_le(&status_payload[4], sequence_number);
        status_payload[6] = UCI_DATA_TRANSFER_STATUS_INVALID_FORMAT;
        status_payload[7] = 0;
        result->notification.mt = UCI_MT_NOTIFICATION;
        result->notification.pbf = UCI_PBF_COMPLETE;
        result->notification.gid = UCI_GID_SESSION_CONTROL;
        result->notification.oid = UCI_SESSION_DATA_TRANSFER_STATUS_NTF;
        result->notification.payload_len = sizeof(status_payload);
        memcpy(result->notification.payload, status_payload, sizeof(status_payload));
        result->has_notification = 1;
        return -1;
    }

    if (session == NULL || session->state != UCI_SESSION_STATE_ACTIVE) {
        write_u32_le(status_payload, session_id);
        write_u16_le(&status_payload[4], sequence_number);
        status_payload[6] = UCI_DATA_TRANSFER_STATUS_ERROR_REJECTED;
        status_payload[7] = 0;
        result->notification.mt = UCI_MT_NOTIFICATION;
        result->notification.pbf = UCI_PBF_COMPLETE;
        result->notification.gid = UCI_GID_SESSION_CONTROL;
        result->notification.oid = UCI_SESSION_DATA_TRANSFER_STATUS_NTF;
        result->notification.payload_len = sizeof(status_payload);
        memcpy(result->notification.payload, status_payload, sizeof(status_payload));
        result->has_notification = 1;
        return -1;
    }

    if (session->has_last_data_message &&
        session->last_data_sequence == sequence_number &&
        session->last_data_length == declared_length) {
        transfer_status = UCI_DATA_TRANSFER_STATUS_REPETITION_OK;
    } else {
        transfer_status = UCI_DATA_TRANSFER_STATUS_OK;
    }

    session->last_data_sequence = sequence_number;
    session->last_data_length = declared_length;
    session->has_last_data_message = 1;

    write_u32_le(credit_payload, session_id);
    credit_payload[4] = 1;
    result->notification.mt = UCI_MT_NOTIFICATION;
    result->notification.pbf = UCI_PBF_COMPLETE;
    result->notification.gid = UCI_GID_SESSION_CONTROL;
    result->notification.oid = UCI_SESSION_DATA_CREDIT_NTF;
    result->notification.payload_len = sizeof(credit_payload);
    memcpy(result->notification.payload, credit_payload, sizeof(credit_payload));
    result->has_notification = 1;

    write_u32_le(status_payload, session_id);
    write_u16_le(&status_payload[4], sequence_number);
    status_payload[6] = transfer_status;
    status_payload[7] = 1;

    {
        uci_sim_packet_t notification;
        memset(&notification, 0, sizeof(notification));
        notification.mt = UCI_MT_NOTIFICATION;
        notification.pbf = UCI_PBF_COMPLETE;
        notification.gid = UCI_GID_SESSION_CONTROL;
        notification.oid = UCI_SESSION_DATA_TRANSFER_STATUS_NTF;
        notification.payload_len = sizeof(status_payload);
        memcpy(notification.payload, status_payload, sizeof(status_payload));
        if (uci_sim_device_queue_notification(device, &notification) != 0) {
            return -1;
        }
    }

    return 0;
}
