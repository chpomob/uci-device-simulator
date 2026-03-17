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
    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (!device->sessions[i].allocated) {
            device->sessions[i].allocated = 1;
            device->sessions[i].session_id = session_id;
            device->sessions[i].session_type = UCI_SESSION_TYPE_RANGING;
            device->sessions[i].state = UCI_SESSION_STATE_INIT;
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

static void write_u32_le(uint8_t* payload, uint32_t value) {
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)((value >> 8) & 0xFFU);
    payload[2] = (uint8_t)((value >> 16) & 0xFFU);
    payload[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void emit_session_status_ntf(uci_sim_device_t* device,
                                    uint32_t session_id,
                                    uint8_t state,
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
    notification.payload[5] = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS;

    (void)uci_sim_device_deliver_notification(device, &notification, result);
}

static int handle_core(uci_sim_device_t* device, const uci_sim_packet_t* request, uci_sim_result_t* result) {
    (void)device;
    switch (request->oid) {
        case UCI_CORE_DEVICE_INFO:
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_CORE;
            result->response.oid = UCI_CORE_DEVICE_INFO;
            result->response.payload_len = 9;
            result->response.payload[0] = UCI_STATUS_OK;
            result->response.payload[1] = 0x00;
            result->response.payload[2] = 0x01;
            result->response.payload[3] = 0x00;
            result->response.payload[4] = 0x02;
            result->response.payload[5] = 0x00;
            result->response.payload[6] = 0x02;
            result->response.payload[7] = 0x00;
            result->response.payload[8] = 0x01;
            return 0;
        case UCI_CORE_GET_CAPS_INFO:
            result->has_response = 1;
            result->response.mt = UCI_MT_RESPONSE;
            result->response.pbf = UCI_PBF_COMPLETE;
            result->response.gid = UCI_GID_CORE;
            result->response.oid = UCI_CORE_GET_CAPS_INFO;
            result->response.payload_len = 4;
            result->response.payload[0] = UCI_STATUS_OK;
            result->response.payload[1] = 0x01;
            result->response.payload[2] = 0xE4;
            result->response.payload[3] = 0x00;
            return 0;
        default:
            make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
            return -1;
    }
}

static int handle_session_config(uci_sim_device_t* device, const uci_sim_packet_t* request, uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;

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
            emit_session_status_ntf(device, session_id, UCI_SESSION_STATE_INIT, result);
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
        default:
            make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
            return -1;
    }
}

static int handle_session_control(uci_sim_device_t* device, const uci_sim_packet_t* request, uci_sim_result_t* result) {
    uint32_t session_id;
    uci_sim_session_t* session;
    uint8_t next_state;

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
        case UCI_SESSION_START:
            next_state = UCI_SESSION_STATE_ACTIVE;
            break;
        case UCI_SESSION_STOP:
            next_state = UCI_SESSION_STATE_IDLE;
            break;
        default:
            make_status_response(request, result, UCI_STATUS_UNKNOWN_OID);
            return -1;
    }

    session->state = next_state;
    make_status_response(request, result, UCI_STATUS_OK);
    emit_session_status_ntf(device, session_id, next_state, result);
    return 0;
}

int uci_sim_device_handle_packet(uci_sim_device_t* device,
                                 const uci_sim_packet_t* request,
                                 uci_sim_result_t* result) {
    int rc;

    if (!device || !request || !result) {
        return -1;
    }

    init_result(result);
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

    uci_sim_device_finalize_result(device, result);
    return rc;
}
