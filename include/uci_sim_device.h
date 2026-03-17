#ifndef UCI_SIM_DEVICE_H
#define UCI_SIM_DEVICE_H

#include "uci_sim_packet.h"

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
    uci_sim_session_t sessions[UCI_SIM_MAX_SESSIONS];
} uci_sim_device_t;

typedef struct {
    uci_sim_packet_t response;
    int has_response;
    uci_sim_packet_t notification;
    int has_notification;
} uci_sim_result_t;

void uci_sim_device_init(uci_sim_device_t* device);
int uci_sim_device_handle_packet(uci_sim_device_t* device,
                                 const uci_sim_packet_t* request,
                                 uci_sim_result_t* result);

#endif
