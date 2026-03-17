#ifndef UCI_SIM_PACKET_H
#define UCI_SIM_PACKET_H

#include "uci_sim_spec.h"

typedef struct {
    uint8_t mt;
    uint8_t pbf;
    uint8_t gid;
    uint8_t oid;
    uint16_t payload_len;
    uint8_t payload[UCI_SIM_MAX_PAYLOAD];
} uci_sim_packet_t;

int uci_sim_packet_parse(const uint8_t* buffer, size_t buffer_len, uci_sim_packet_t* packet);
int uci_sim_packet_serialize(const uci_sim_packet_t* packet, uint8_t* buffer, size_t buffer_capacity, size_t* written);

#endif
