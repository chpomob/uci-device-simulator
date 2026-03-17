#ifndef UCI_SIM_DEVICE_H
#define UCI_SIM_DEVICE_H

#include "uci_sim_packet.h"
#include "uci_sim_scenario.h"

#define UCI_SIM_MAX_PENDING_NOTIFICATIONS 8U

typedef struct {
    uint32_t session_id;
    uint8_t session_type;
    uint8_t state;
    int allocated;
} uci_sim_session_t;

typedef struct {
    uint16_t uci_version;
    uint16_t mac_version;
    uint16_t phy_version;
    uint16_t test_version;
    uint8_t device_state;
    uci_sim_scenario_kind_t scenario;
    uci_sim_session_t sessions[UCI_SIM_MAX_SESSIONS];
    uci_sim_packet_t pending_notifications[UCI_SIM_MAX_PENDING_NOTIFICATIONS];
    size_t pending_notification_count;
} uci_sim_device_t;

typedef struct {
    uci_sim_packet_t response;
    int has_response;
    uci_sim_packet_t notification;
    int has_notification;
} uci_sim_result_t;

void uci_sim_device_init(uci_sim_device_t* device);
void uci_sim_device_init_with_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario);
void uci_sim_device_set_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario);
int uci_sim_device_queue_notification(uci_sim_device_t* device, const uci_sim_packet_t* notification);
int uci_sim_device_dequeue_notification(uci_sim_device_t* device, uci_sim_packet_t* notification);
int uci_sim_device_deliver_notification(uci_sim_device_t* device,
                                        const uci_sim_packet_t* notification,
                                        uci_sim_result_t* result);
void uci_sim_device_finalize_result(uci_sim_device_t* device,
                                  uci_sim_result_t* result,
                                  size_t pending_count_before);
int uci_sim_device_handle_packet(uci_sim_device_t* device,
                                 const uci_sim_packet_t* request,
                                 uci_sim_result_t* result);

#endif
