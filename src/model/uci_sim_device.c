#include "uci_sim_device.h"

#include <string.h>

void uci_sim_device_set_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario) {
    if (!device) {
        return;
    }

    device->scenario = scenario;
}

void uci_sim_device_init_with_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario) {
    memset(device, 0, sizeof(*device));
    device->uci_version = 0x0100;
    device->mac_version = 0x0200;
    device->phy_version = 0x0200;
    device->test_version = 0x0100;
    device->device_state = UCI_DEVICE_STATE_READY;
    device->device_configs[0].in_use = 1;
    device->device_configs[0].config_id = UCI_DEVICE_CONFIG_DEVICE_STATE;
    device->device_configs[0].value_len = 1;
    device->device_configs[0].value[0] = UCI_DEVICE_STATE_READY;
    device->device_configs[1].in_use = 1;
    device->device_configs[1].config_id = UCI_DEVICE_CONFIG_LOW_POWER_MODE;
    device->device_configs[1].value_len = 1;
    device->device_configs[1].value[0] = 0x00;
    device->device_configs[2].in_use = 1;
    device->device_configs[2].config_id = UCI_DEVICE_CONFIG_DEVICE_PAN_ID;
    device->device_configs[2].value_len = 2;
    device->device_configs[2].value[0] = 0x00;
    device->device_configs[2].value[1] = 0x00;
    device->scenario = scenario;
    device->next_ranging_sequence = 1;
}

void uci_sim_device_init(uci_sim_device_t* device) {
    uci_sim_device_init_with_scenario(device, UCI_SIM_SCENARIO_DEFAULT);
}

int uci_sim_device_queue_notification(uci_sim_device_t* device, const uci_sim_packet_t* notification) {
    if (!device || !notification) {
        return -1;
    }

    if (device->pending_notification_count >= UCI_SIM_MAX_PENDING_NOTIFICATIONS) {
        return -1;
    }

    device->pending_notifications[device->pending_notification_count++] = *notification;
    return 0;
}

int uci_sim_device_dequeue_notification(uci_sim_device_t* device, uci_sim_packet_t* notification) {
    size_t i;

    if (!device || !notification || device->pending_notification_count == 0) {
        return -1;
    }

    *notification = device->pending_notifications[0];
    for (i = 1; i < device->pending_notification_count; ++i) {
        device->pending_notifications[i - 1] = device->pending_notifications[i];
    }
    device->pending_notification_count--;
    return 0;
}

int uci_sim_device_deliver_notification(uci_sim_device_t* device,
                                        const uci_sim_packet_t* notification,
                                        uci_sim_result_t* result) {
    if (!device || !notification || !result) {
        return -1;
    }

    if (uci_sim_scenario_should_defer_notification(device->scenario)) {
        return uci_sim_device_queue_notification(device, notification);
    }

    if (!result->has_notification) {
        result->notification = *notification;
        result->has_notification = 1;
        return 0;
    }

    return uci_sim_device_queue_notification(device, notification);
}

void uci_sim_device_finalize_result(uci_sim_device_t* device,
                                  uci_sim_result_t* result,
                                  size_t pending_count_before) {
    if (!device || !result || result->has_notification) {
        return;
    }
    if (!uci_sim_scenario_should_auto_deliver_pending(device->scenario, pending_count_before)) {
        return;
    }

    if (uci_sim_device_dequeue_notification(device, &result->notification) == 0) {
        result->has_notification = 1;
    }
}

int uci_sim_session_store_config(uci_sim_session_t* session,
                                 uint8_t config_id,
                                 const uint8_t* value,
                                 uint8_t value_len) {
    size_t i;

    if (!session || value_len > UCI_SIM_MAX_CONFIG_VALUE || (value_len > 0 && !value)) {
        return -1;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSION_CONFIGS; ++i) {
        if (session->configs[i].in_use && session->configs[i].config_id == config_id) {
            session->configs[i].value_len = value_len;
            if (value_len > 0) {
                memcpy(session->configs[i].value, value, value_len);
            }
            return 0;
        }
    }

    for (i = 0; i < UCI_SIM_MAX_SESSION_CONFIGS; ++i) {
        if (!session->configs[i].in_use) {
            session->configs[i].in_use = 1;
            session->configs[i].config_id = config_id;
            session->configs[i].value_len = value_len;
            if (value_len > 0) {
                memcpy(session->configs[i].value, value, value_len);
            }
            return 0;
        }
    }

    return -1;
}

int uci_sim_session_get_config(const uci_sim_session_t* session,
                               uint8_t config_id,
                               uint8_t* value,
                               uint8_t* value_len) {
    size_t i;

    if (!session || !value_len) {
        return -1;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSION_CONFIGS; ++i) {
        if (session->configs[i].in_use && session->configs[i].config_id == config_id) {
            if (value && session->configs[i].value_len > 0) {
                memcpy(value, session->configs[i].value, session->configs[i].value_len);
            }
            *value_len = session->configs[i].value_len;
            return 0;
        }
    }

    return -1;
}

int uci_sim_device_store_config(uci_sim_device_t* device,
                                uint8_t config_id,
                                const uint8_t* value,
                                uint8_t value_len) {
    size_t i;

    if (!device || value_len > UCI_SIM_MAX_CONFIG_VALUE || (value_len > 0 && !value)) {
        return -1;
    }

    if (config_id == UCI_DEVICE_CONFIG_DEVICE_STATE) {
        if (value_len != 1) {
            return -1;
        }
        device->device_state = value[0];
    }

    for (i = 0; i < UCI_SIM_MAX_DEVICE_CONFIGS; ++i) {
        if (device->device_configs[i].in_use && device->device_configs[i].config_id == config_id) {
            device->device_configs[i].value_len = value_len;
            if (value_len > 0) {
                memcpy(device->device_configs[i].value, value, value_len);
            }
            return 0;
        }
    }

    for (i = 0; i < UCI_SIM_MAX_DEVICE_CONFIGS; ++i) {
        if (!device->device_configs[i].in_use) {
            device->device_configs[i].in_use = 1;
            device->device_configs[i].config_id = config_id;
            device->device_configs[i].value_len = value_len;
            if (value_len > 0) {
                memcpy(device->device_configs[i].value, value, value_len);
            }
            return 0;
        }
    }

    return -1;
}

int uci_sim_device_get_config(const uci_sim_device_t* device,
                              uint8_t config_id,
                              uint8_t* value,
                              uint8_t* value_len) {
    size_t i;

    if (!device || !value_len) {
        return -1;
    }

    if (config_id == UCI_DEVICE_CONFIG_DEVICE_STATE) {
        if (value) {
            value[0] = device->device_state;
        }
        *value_len = 1;
        return 0;
    }

    for (i = 0; i < UCI_SIM_MAX_DEVICE_CONFIGS; ++i) {
        if (device->device_configs[i].in_use && device->device_configs[i].config_id == config_id) {
            if (value && device->device_configs[i].value_len > 0) {
                memcpy(value, device->device_configs[i].value, device->device_configs[i].value_len);
            }
            *value_len = device->device_configs[i].value_len;
            return 0;
        }
    }

    return -1;
}

static void write_u32_le(uint8_t* payload, uint32_t value) {
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)((value >> 8) & 0xFFU);
    payload[2] = (uint8_t)((value >> 16) & 0xFFU);
    payload[3] = (uint8_t)((value >> 24) & 0xFFU);
}

int uci_sim_device_emit_ranging_stream(uci_sim_device_t* device,
                                       uci_sim_session_t* session,
                                       uci_sim_result_t* result) {
    uci_sim_packet_t notification;
    uint8_t* payload;
    uint32_t sequence_number;
    uint32_t measurement_base_cm;

    if (!device || !session || !result) {
        return -1;
    }
    if (device->scenario != UCI_SIM_SCENARIO_RANGING_STREAM ||
        session->ranging_stream_remaining == 0 ||
        session->state != UCI_SESSION_STATE_ACTIVE) {
        return 0;
    }

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONTROL;
    notification.oid = UCI_SESSION_START;
    notification.payload_len = 52;
    payload = notification.payload;

    sequence_number = device->next_ranging_sequence++;
    measurement_base_cm = 100U + (session->ranging_count * 5U);

    write_u32_le(&payload[0], sequence_number);
    write_u32_le(&payload[4], session->session_id);
    payload[8] = 0x00;
    write_u32_le(&payload[9], 1000U);
    payload[13] = 0x01;
    payload[14] = 0x00;
    payload[15] = 0x00;
    write_u32_le(&payload[16], session->session_id);
    memset(&payload[20], 0, 4);
    payload[24] = 0x01;
    payload[25] = 0x12;
    payload[26] = 0x34;
    payload[27] = 0x00;
    payload[28] = 0x00;
    payload[29] = (uint8_t)(measurement_base_cm & 0xFFU);
    payload[30] = (uint8_t)((measurement_base_cm >> 8) & 0xFFU);
    payload[31] = 0x14;
    payload[32] = 0x00;
    payload[33] = 0x08;
    payload[34] = 0x05;
    payload[35] = 0x00;
    payload[36] = 0x07;
    payload[37] = 0x10;
    payload[38] = 0x00;
    payload[39] = 0x06;
    payload[40] = 0x03;
    payload[41] = 0x00;
    payload[42] = 0x09;
    payload[43] = 0x02;
    payload[44] = 0xE0;
    memset(&payload[45], 0, 7);

    session->ranging_count++;
    session->ranging_stream_remaining--;
    if (uci_sim_device_queue_notification(device, &notification) != 0) {
        return -1;
    }

    return 0;
}
