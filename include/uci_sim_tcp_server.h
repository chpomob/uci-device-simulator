#ifndef UCI_SIM_TCP_SERVER_H
#define UCI_SIM_TCP_SERVER_H

#include "uci_sim_device.h"

#include <stdint.h>

int uci_sim_tcp_serve(const char* host, uint16_t port, uci_sim_device_t* device);

#endif
