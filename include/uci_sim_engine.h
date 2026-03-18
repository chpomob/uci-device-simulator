#ifndef UCI_SIM_ENGINE_H
#define UCI_SIM_ENGINE_H

#include "uci_sim_device.h"

#define UCI_SIM_MAX_OUTBOUND_PACKETS 16U

typedef struct {
    uci_sim_device_t device;
    uci_sim_packet_t outbound_packets[UCI_SIM_MAX_OUTBOUND_PACKETS];
    size_t outbound_count;
} uci_sim_engine_t;

void uci_sim_engine_init(uci_sim_engine_t* engine);
void uci_sim_engine_init_with_scenario(uci_sim_engine_t* engine, uci_sim_scenario_kind_t scenario);
int uci_sim_engine_submit_packet(uci_sim_engine_t* engine, const uci_sim_packet_t* request);
int uci_sim_engine_tick(uci_sim_engine_t* engine, uint32_t elapsed_ms);
int uci_sim_engine_dequeue_outbound_packet(uci_sim_engine_t* engine, uci_sim_packet_t* packet);

#endif
