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
    device->scenario = scenario;
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

    if (device->scenario == UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS) {
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
    if (!device || !result || result->has_notification || pending_count_before == 0) {
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
