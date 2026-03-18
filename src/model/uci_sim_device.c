#include "uci_sim_device.h"

#include <string.h>

void uci_sim_device_set_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario) {
    if (!device) {
        return;
    }

    device->scenario = scenario;
}

void uci_sim_device_init_with_profile(uci_sim_device_t* device,
                                      const uci_sim_profile_t* profile,
                                      uci_sim_scenario_kind_t scenario) {
    const uci_sim_profile_t* effective_profile = profile ? profile : uci_sim_default_profile();

    memset(device, 0, sizeof(*device));
    device->profile = effective_profile;
    device->uci_version = effective_profile->uci_version;
    device->mac_version = effective_profile->mac_version;
    device->phy_version = effective_profile->phy_version;
    device->test_version = effective_profile->test_version;
    device->device_state = effective_profile->default_device_state;
    device->next_uwbs_timestamp = effective_profile->initial_uwbs_timestamp;
    device->device_configs[0].in_use = 1;
    device->device_configs[0].config_id = UCI_DEVICE_CONFIG_DEVICE_STATE;
    device->device_configs[0].value_len = 1;
    device->device_configs[0].value[0] = effective_profile->default_device_state;
    device->device_configs[1].in_use = 1;
    device->device_configs[1].config_id = UCI_DEVICE_CONFIG_LOW_POWER_MODE;
    device->device_configs[1].value_len = 1;
    device->device_configs[1].value[0] = effective_profile->default_low_power_mode;
    device->device_configs[2].in_use = 1;
    device->device_configs[2].config_id = UCI_DEVICE_CONFIG_DEVICE_PAN_ID;
    device->device_configs[2].value_len = 2;
    device->device_configs[2].value[0] = effective_profile->default_device_pan_id[0];
    device->device_configs[2].value[1] = effective_profile->default_device_pan_id[1];
    device->scenario = scenario;
    device->next_ranging_sequence = 1;
}

void uci_sim_device_init_with_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario) {
    uci_sim_device_init_with_profile(device, uci_sim_default_profile(), scenario);
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

int uci_sim_device_schedule_event(uci_sim_device_t* device,
                                  uci_sim_event_type_t type,
                                  uint32_t session_id,
                                  uint32_t delay_ms) {
    uci_sim_scheduled_event_t* event;

    if (!device || type == UCI_SIM_EVENT_NONE) {
        return -1;
    }
    if (device->scheduled_event_count >= UCI_SIM_MAX_SCENARIO_EVENTS) {
        return -1;
    }

    event = &device->scheduled_events[device->scheduled_event_count++];
    event->type = type;
    event->session_id = session_id;
    event->delay_ms = delay_ms;
    return 0;
}

void uci_sim_device_cancel_session_events(uci_sim_device_t* device, uint32_t session_id) {
    size_t i;
    size_t out = 0;

    if (!device) {
        return;
    }

    for (i = 0; i < device->scheduled_event_count; ++i) {
        if (device->scheduled_events[i].session_id == session_id) {
            continue;
        }
        if (out != i) {
            device->scheduled_events[out] = device->scheduled_events[i];
        }
        out++;
    }
    device->scheduled_event_count = out;
}

void uci_sim_device_tick_events(uci_sim_device_t* device, uint32_t elapsed_ms) {
    size_t i;

    if (!device) {
        return;
    }

    for (i = 0; i < device->scheduled_event_count; ++i) {
        if (device->scheduled_events[i].delay_ms > elapsed_ms) {
            device->scheduled_events[i].delay_ms -= elapsed_ms;
        } else {
            device->scheduled_events[i].delay_ms = 0;
        }
    }
}

int uci_sim_device_dequeue_ready_event(uci_sim_device_t* device, uci_sim_scheduled_event_t* event) {
    size_t i;

    if (!device || !event) {
        return -1;
    }

    for (i = 0; i < device->scheduled_event_count; ++i) {
        if (device->scheduled_events[i].delay_ms == 0) {
            *event = device->scheduled_events[i];
            for (; i + 1 < device->scheduled_event_count; ++i) {
                device->scheduled_events[i] = device->scheduled_events[i + 1];
            }
            device->scheduled_event_count--;
            return 0;
        }
    }

    return -1;
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

static uci_sim_session_t* find_session_by_id(uci_sim_device_t* device, uint32_t session_id) {
    size_t i;

    if (!device) {
        return NULL;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (device->sessions[i].allocated && device->sessions[i].session_id == session_id) {
            return &device->sessions[i];
        }
    }

    return NULL;
}

int uci_sim_device_get_session(uci_sim_device_t* device,
                               uint32_t session_id,
                               uci_sim_session_t** out_session) {
    uci_sim_session_t* session;

    if (!out_session) {
        return -1;
    }

    session = find_session_by_id(device, session_id);
    if (!session) {
        return -1;
    }

    *out_session = session;
    return 0;
}

int uci_sim_device_emit_ranging_stream(uci_sim_device_t* device,
                                       uci_sim_session_t* session,
                                       uci_sim_result_t* result) {
    uci_sim_packet_t notification;
    uint8_t* payload;
    const uci_sim_profile_t* profile;
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

    profile = device->profile ? device->profile : uci_sim_default_profile();
    if (profile->range_data_payload_len == 0 ||
        profile->range_data_payload_len > UCI_SIM_MAX_PAYLOAD) {
        return -1;
    }

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONTROL;
    notification.oid = profile->range_data_notification_oid;
    notification.payload_len = profile->range_data_payload_len;
    payload = notification.payload;
    memcpy(payload, profile->range_data_payload_template, profile->range_data_payload_len);

    sequence_number = device->next_ranging_sequence++;
    measurement_base_cm = profile->range_data_distance_base_cm +
                          (session->ranging_count * profile->range_data_distance_step_cm);

    write_u32_le(&payload[profile->range_data_sequence_offset], sequence_number);
    write_u32_le(&payload[profile->range_data_primary_session_id_offset], session->session_id);
    write_u32_le(&payload[profile->range_data_secondary_session_id_offset], session->session_id);
    write_u32_le(&payload[profile->range_data_interval_offset], profile->ranging_interval_ms);
    payload[profile->range_data_measurement_distance_offset] = (uint8_t)(measurement_base_cm & 0xFFU);
    payload[profile->range_data_measurement_distance_offset + 1] =
        (uint8_t)((measurement_base_cm >> 8) & 0xFFU);

    session->ranging_count++;
    session->ranging_stream_remaining--;
    if (uci_sim_device_queue_notification(device, &notification) != 0) {
        return -1;
    }

    return 0;
}
