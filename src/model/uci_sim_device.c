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

    if (!result->has_notification) {
        result->notification = *notification;
        result->has_notification = 1;
        return 0;
    }

    return uci_sim_device_queue_notification(device, notification);
}

void uci_sim_device_finalize_result(uci_sim_device_t* device, uci_sim_result_t* result) {
    if (!device || !result || result->has_notification) {
        return;
    }

    if (uci_sim_device_dequeue_notification(device, &result->notification) == 0) {
        result->has_notification = 1;
    }
}
