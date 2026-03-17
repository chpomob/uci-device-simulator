#include "uci_sim_device.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int uci_sim_tcp_serve(const char* host, uint16_t port, uci_sim_device_t* device);

int main(int argc, char** argv) {
    uci_sim_device_t device;
    const char* host = "127.0.0.1";
    uint16_t port = 9000;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = (uint16_t)strtoul(argv[2], NULL, 10);
    }

    uci_sim_device_init(&device);
    return uci_sim_tcp_serve(host, port, &device);
}
