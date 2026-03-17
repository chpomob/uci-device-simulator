#include "uci_sim_packet.h"

#include <string.h>

static uint16_t payload_length_from_header(uint8_t mt, uint8_t third, uint8_t fourth) {
    if (mt == UCI_MT_DATA) {
        return (uint16_t)((uint16_t)third | ((uint16_t)fourth << 8));
    }
    return fourth;
}

int uci_sim_packet_parse(const uint8_t* buffer, size_t buffer_len, uci_sim_packet_t* packet) {
    if (!buffer || !packet || buffer_len < UCI_SIM_HEADER_SIZE) {
        return -1;
    }

    packet->mt = (buffer[0] >> 5) & 0x03;
    packet->pbf = (buffer[0] >> 4) & 0x01;
    packet->gid = buffer[0] & 0x0F;
    packet->oid = buffer[1] & 0x3F;
    packet->payload_len = payload_length_from_header(packet->mt, buffer[2], buffer[3]);

    if (packet->payload_len > UCI_SIM_MAX_PAYLOAD) {
        return -1;
    }
    if (buffer_len < UCI_SIM_HEADER_SIZE + packet->payload_len) {
        return -1;
    }

    if (packet->payload_len > 0) {
        memcpy(packet->payload, buffer + UCI_SIM_HEADER_SIZE, packet->payload_len);
    }
    return 0;
}

int uci_sim_packet_serialize(const uci_sim_packet_t* packet, uint8_t* buffer, size_t buffer_capacity, size_t* written) {
    if (!packet || !buffer || !written) {
        return -1;
    }
    if (packet->payload_len > UCI_SIM_MAX_PAYLOAD || buffer_capacity < UCI_SIM_HEADER_SIZE + packet->payload_len) {
        return -1;
    }

    buffer[0] = (uint8_t)(((packet->mt & 0x03) << 5) | ((packet->pbf & 0x01) << 4) | (packet->gid & 0x0F));
    buffer[1] = packet->oid & 0x3F;
    if (packet->mt == UCI_MT_DATA) {
        buffer[2] = (uint8_t)(packet->payload_len & 0xFF);
        buffer[3] = (uint8_t)((packet->payload_len >> 8) & 0xFF);
    } else {
        buffer[2] = 0;
        buffer[3] = (uint8_t)(packet->payload_len & 0xFF);
    }

    if (packet->payload_len > 0) {
        memcpy(buffer + UCI_SIM_HEADER_SIZE, packet->payload, packet->payload_len);
    }
    *written = UCI_SIM_HEADER_SIZE + packet->payload_len;
    return 0;
}
