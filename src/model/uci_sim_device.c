#include "uci_sim_device.h"
#include "uci_sim_measurement.h"

#include <string.h>

static void apply_profile_defaults(uci_sim_device_t* device, const uci_sim_profile_t* effective_profile) {
    if (!device || !effective_profile) {
        return;
    }

    device->profile = effective_profile;
    device->uci_version = effective_profile->uci_version;
    device->mac_version = effective_profile->mac_version;
    device->phy_version = effective_profile->phy_version;
    device->test_version = effective_profile->test_version;
    device->device_state = effective_profile->default_device_state;
    device->next_uwbs_timestamp = effective_profile->initial_uwbs_timestamp;

    memset(device->device_configs, 0, sizeof(device->device_configs));
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
}

static uint16_t read_u16_le(const uint8_t* payload) {
    return (uint16_t)payload[0] |
           (uint16_t)((uint16_t)payload[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* payload) {
    return (uint32_t)payload[0] |
           ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) |
           ((uint32_t)payload[3] << 24);
}

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
    apply_profile_defaults(device, effective_profile);
    device->scenario = scenario;
    device->next_ranging_sequence = 1;
}

void uci_sim_device_init_with_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario) {
    uci_sim_device_init_with_profile(device, uci_sim_default_profile(), scenario);
}

void uci_sim_device_init(uci_sim_device_t* device) {
    uci_sim_device_init_with_scenario(device, UCI_SIM_SCENARIO_DEFAULT);
}

void uci_sim_device_reset_runtime_state(uci_sim_device_t* device) {
    const uci_sim_profile_t* effective_profile;
    uci_sim_scenario_kind_t scenario;

    if (!device) {
        return;
    }

    effective_profile = device->profile ? device->profile : uci_sim_default_profile();
    scenario = device->scenario;

    memset(device->sessions, 0, sizeof(device->sessions));
    memset(device->scheduled_events, 0, sizeof(device->scheduled_events));
    memset(device->pending_notifications, 0, sizeof(device->pending_notifications));
    device->scheduled_event_count = 0;
    device->pending_notification_count = 0;
    device->next_ranging_sequence = 1;
    apply_profile_defaults(device, effective_profile);
    device->scenario = scenario;
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

int uci_sim_device_reschedule_session_event(uci_sim_device_t* device,
                                            uci_sim_event_type_t type,
                                            uint32_t session_id,
                                            uint32_t delay_ms) {
    size_t i;
    size_t out = 0;

    if (!device || type == UCI_SIM_EVENT_NONE) {
        return -1;
    }

    for (i = 0; i < device->scheduled_event_count; ++i) {
        if (device->scheduled_events[i].type == type &&
            device->scheduled_events[i].session_id == session_id) {
            continue;
        }
        if (out != i) {
            device->scheduled_events[out] = device->scheduled_events[i];
        }
        out++;
    }
    device->scheduled_event_count = out;

    return uci_sim_device_schedule_event(device, type, session_id, delay_ms);
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

int uci_sim_device_queue_data_credit_notification(uci_sim_device_t* device,
                                                  uint32_t session_id,
                                                  uint8_t availability) {
    uci_sim_packet_t notification;

    if (!device) {
        return -1;
    }

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONTROL;
    notification.oid = UCI_SESSION_DATA_CREDIT_NTF;
    notification.payload_len = 5;
    notification.payload[0] = (uint8_t)(session_id & 0xFFU);
    notification.payload[1] = (uint8_t)((session_id >> 8) & 0xFFU);
    notification.payload[2] = (uint8_t)((session_id >> 16) & 0xFFU);
    notification.payload[3] = (uint8_t)((session_id >> 24) & 0xFFU);
    notification.payload[4] = availability;
    return uci_sim_device_queue_notification(device, &notification);
}

int uci_sim_device_queue_data_transfer_status_notification(uci_sim_device_t* device,
                                                           uint32_t session_id,
                                                           uint16_t sequence_number,
                                                           uint8_t status,
                                                           uint8_t tx_count) {
    uci_sim_packet_t notification;

    if (!device) {
        return -1;
    }

    memset(&notification, 0, sizeof(notification));
    notification.mt = UCI_MT_NOTIFICATION;
    notification.pbf = UCI_PBF_COMPLETE;
    notification.gid = UCI_GID_SESSION_CONTROL;
    notification.oid = UCI_SESSION_DATA_TRANSFER_STATUS_NTF;
    notification.payload_len = 8;
    notification.payload[0] = (uint8_t)(session_id & 0xFFU);
    notification.payload[1] = (uint8_t)((session_id >> 8) & 0xFFU);
    notification.payload[2] = (uint8_t)((session_id >> 16) & 0xFFU);
    notification.payload[3] = (uint8_t)((session_id >> 24) & 0xFFU);
    notification.payload[4] = (uint8_t)(sequence_number & 0xFFU);
    notification.payload[5] = (uint8_t)((sequence_number >> 8) & 0xFFU);
    notification.payload[6] = status;
    notification.payload[7] = tx_count;
    return uci_sim_device_queue_notification(device, &notification);
}

int uci_sim_device_progress_data_transfer(uci_sim_device_t* device,
                                          uci_sim_session_t* session) {
    if (!device || !session || !session->allocated || !session->data_transfer_in_progress) {
        return -1;
    }

    session->data_transfer_tx_count++;

    if (session->data_transfer_repetitions_remaining > 0U) {
        session->data_transfer_repetitions_remaining--;
        return uci_sim_device_queue_data_transfer_status_notification(device,
                                                                      session->session_id,
                                                                      session->data_transfer_sequence,
                                                                      UCI_DATA_TRANSFER_STATUS_REPETITION_OK,
                                                                      session->data_transfer_tx_count);
    }

    session->data_transfer_in_progress = 0;
    if (uci_sim_device_queue_data_credit_notification(device, session->session_id, 1U) != 0) {
        return -1;
    }
    return uci_sim_device_queue_data_transfer_status_notification(device,
                                                                  session->session_id,
                                                                  session->data_transfer_sequence,
                                                                  UCI_DATA_TRANSFER_STATUS_OK,
                                                                  session->data_transfer_tx_count);
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

uint8_t uci_sim_session_get_range_data_ntf_config(const uci_sim_session_t* session) {
    uint8_t value = 0x01;
    uint8_t value_len = 0;

    if (!session) {
        return 0x01;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_SESSION_INFO_NTF_CONFIG, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x01;
    }

    return value;
}

uint16_t uci_sim_session_get_range_data_ntf_proximity_near(const uci_sim_session_t* session) {
    uint8_t value[2] = {0x00, 0x00};
    uint8_t value_len = 0;

    if (!session) {
        return 0;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_NEAR, value, &value_len) != 0 ||
        value_len != 2) {
        return 0;
    }

    return read_u16_le(value);
}

uint16_t uci_sim_session_get_range_data_ntf_proximity_far(const uci_sim_session_t* session) {
    uint8_t value[2] = {0xFF, 0xFF};
    uint8_t value_len = 0;

    if (!session) {
        return 0xFFFFU;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_FAR, value, &value_len) != 0 ||
        value_len != 2) {
        return 0xFFFFU;
    }

    return read_u16_le(value);
}

uint8_t uci_sim_session_get_ranging_round_usage(const uci_sim_session_t* session) {
    uint8_t value = 0x01;
    uint8_t value_len = 0;

    if (!session) {
        return 0x01;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_RANGING_ROUND_USAGE, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x01;
    }

    return value;
}

uint8_t uci_sim_session_get_data_repetition_count(const uci_sim_session_t* session) {
    uint8_t value = 0x00;
    uint8_t value_len = 0;

    if (!session) {
        return 0x00;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_DATA_REPETITION_COUNT, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x00;
    }

    return value;
}

uint32_t uci_sim_session_get_ranging_interval_ms(const uci_sim_session_t* session,
                                                 const uci_sim_profile_t* profile) {
    uint8_t value[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t value_len = 0;

    if (!session) {
        return profile ? profile->ranging_interval_ms : 0U;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_RANGING_INTERVAL, value, &value_len) != 0 ||
        value_len != 4) {
        return profile ? profile->ranging_interval_ms : 0U;
    }

    return read_u32_le(value);
}

uint8_t uci_sim_session_get_slots_per_rr(const uci_sim_session_t* session) {
    uint8_t value = 0x01;
    uint8_t value_len = 0;

    if (!session) {
        return 0x01;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_SLOTS_PER_RR, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x01;
    }

    return value;
}

uint8_t uci_sim_session_get_responder_slot_index(const uci_sim_session_t* session) {
    uint8_t value = 0x00;
    uint8_t value_len = 0;

    if (!session) {
        return 0x00;
    }

    if (uci_sim_session_get_config(session, 0x1E, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x00;
    }

    return value;
}

uint8_t uci_sim_session_get_aoa_result_req(const uci_sim_session_t* session) {
    uint8_t value = 0x00;
    uint8_t value_len = 0;

    if (!session) {
        return 0x00;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_AOA_RESULT_REQ, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x00;
    }

    return value;
}

uint8_t uci_sim_session_get_rssi_reporting(const uci_sim_session_t* session) {
    uint8_t value = 0x00;
    uint8_t value_len = 0;

    if (!session) {
        return 0x00;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_RSSI_REPORTING, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x00;
    }

    return value;
}

uint8_t uci_sim_session_get_result_report_config(const uci_sim_session_t* session) {
    uint8_t value = 0x01;
    uint8_t value_len = 0;

    if (!session) {
        return 0x01;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_RESULT_REPORT_CONFIG, &value, &value_len) != 0 ||
        value_len != 1) {
        return 0x01;
    }

    return value;
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

int uci_sim_session_add_multicast_entry(uci_sim_session_t* session,
                                        uint16_t short_address,
                                        uint32_t subsession_id,
                                        const uint8_t* key,
                                        uint8_t key_len) {
    size_t i;

    if (!session || key_len > 32 || (key_len > 0 && !key)) {
        return -1;
    }

    for (i = 0; i < UCI_SIM_MAX_MULTICAST_ENTRIES; ++i) {
        if (session->multicast_entries[i].in_use &&
            session->multicast_entries[i].short_address == short_address &&
            session->multicast_entries[i].subsession_id == subsession_id) {
            return UCI_STATUS_ADDRESS_ALREADY_PRESENT;
        }
    }

    for (i = 0; i < UCI_SIM_MAX_MULTICAST_ENTRIES; ++i) {
        if (!session->multicast_entries[i].in_use) {
            session->multicast_entries[i].in_use = 1;
            session->multicast_entries[i].short_address = short_address;
            session->multicast_entries[i].subsession_id = subsession_id;
            session->multicast_entries[i].key_len = key_len;
            if (key_len > 0) {
                memcpy(session->multicast_entries[i].key, key, key_len);
            }
            return UCI_STATUS_OK;
        }
    }

    return UCI_STATUS_MULTICAST_LIST_FULL;
}

int uci_sim_session_remove_multicast_entry(uci_sim_session_t* session,
                                           uint16_t short_address,
                                           uint32_t subsession_id) {
    size_t i;

    if (!session) {
        return -1;
    }

    for (i = 0; i < UCI_SIM_MAX_MULTICAST_ENTRIES; ++i) {
        if (session->multicast_entries[i].in_use &&
            session->multicast_entries[i].short_address == short_address &&
            session->multicast_entries[i].subsession_id == subsession_id) {
            memset(&session->multicast_entries[i], 0, sizeof(session->multicast_entries[i]));
            return UCI_STATUS_OK;
        }
    }

    return UCI_STATUS_ADDRESS_NOT_FOUND;
}

uci_sim_logical_link_t* uci_sim_session_find_logical_link(uci_sim_session_t* session,
                                                          uint8_t link_id) {
    uint8_t i;

    if (!session) {
        return NULL;
    }

    for (i = 0; i < session->logical_link_count; ++i) {
        if (session->logical_links[i].in_use && session->logical_links[i].link_id == link_id) {
            return &session->logical_links[i];
        }
    }

    return NULL;
}

uci_sim_logical_link_t* uci_sim_session_allocate_logical_link(uci_sim_session_t* session,
                                                              uint8_t requested_id,
                                                              uint8_t* assigned_id) {
    uci_sim_logical_link_t* entry;
    uint8_t candidate;

    if (!session || !assigned_id || session->logical_link_count >= UCI_SIM_MAX_LOGICAL_LINKS) {
        return NULL;
    }

    candidate = requested_id;
    if (candidate == 0xFF) {
        candidate = 0;
        while (uci_sim_session_find_logical_link(session, candidate) && candidate < 0xFF) {
            candidate++;
        }
        if (candidate == 0xFF && uci_sim_session_find_logical_link(session, candidate)) {
            return NULL;
        }
    }

    entry = &session->logical_links[session->logical_link_count++];
    memset(entry, 0, sizeof(*entry));
    entry->in_use = 1;
    entry->link_id = candidate;
    *assigned_id = candidate;
    return entry;
}

int uci_sim_session_remove_logical_link(uci_sim_session_t* session, uint8_t link_id) {
    uint8_t i;

    if (!session) {
        return -1;
    }

    for (i = 0; i < session->logical_link_count; ++i) {
        if (session->logical_links[i].in_use && session->logical_links[i].link_id == link_id) {
            for (; i + 1 < session->logical_link_count; ++i) {
                session->logical_links[i] = session->logical_links[i + 1];
            }
            if (session->logical_link_count > 0) {
                session->logical_link_count--;
            }
            if (session->logical_link_count < UCI_SIM_MAX_LOGICAL_LINKS) {
                memset(&session->logical_links[session->logical_link_count],
                       0,
                       sizeof(session->logical_links[session->logical_link_count]));
            }
            return 0;
        }
    }

    return -1;
}

int uci_sim_device_emit_ranging_stream(uci_sim_device_t* device,
                                       uci_sim_session_t* session,
                                       uci_sim_result_t* result) {
    uci_sim_packet_t notification;
    uci_sim_measurement_t measurement;
    uci_sim_measurement_policy_result_t policy_result;
    const uci_sim_profile_t* profile;

    if (!device || !session || !result) {
        return -1;
    }
    if (device->scenario != UCI_SIM_SCENARIO_RANGING_STREAM ||
        session->ranging_stream_remaining == 0 ||
        session->state != UCI_SESSION_STATE_ACTIVE) {
        return 0;
    }

    profile = device->profile ? device->profile : uci_sim_default_profile();
    uci_sim_measurement_init_ranging_sample(profile, session, &measurement);
    uci_sim_measurement_evaluate_range_notification_policy(session, &measurement, &policy_result);

    if (policy_result.should_emit_notification) {
        measurement.sequence_number = device->next_ranging_sequence++;
        if (uci_sim_measurement_build_range_data_notification(profile,
                                                              &measurement,
                                                              &policy_result,
                                                              profile->range_data_notification_oid,
                                                              &notification) != 0) {
            return -1;
        }
        if (uci_sim_device_queue_notification(device, &notification) != 0) {
            return -1;
        }
    }

    session->has_last_proximity_state = policy_result.has_proximity_state;
    session->last_in_proximity_range = policy_result.in_proximity_range;
    session->ranging_count++;
    session->ranging_stream_remaining--;
    return 0;
}
