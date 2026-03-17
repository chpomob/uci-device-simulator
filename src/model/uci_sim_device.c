#include "uci_sim_device.h"

#include <string.h>

void uci_sim_device_init(uci_sim_device_t* device) {
    memset(device, 0, sizeof(*device));
    device->uci_version = 0x0100;
    device->mac_version = 0x0200;
    device->phy_version = 0x0200;
    device->test_version = 0x0100;
    device->device_state = UCI_DEVICE_STATE_READY;
}
