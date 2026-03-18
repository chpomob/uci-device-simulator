#ifndef UCI_SIM_PROFILE_H
#define UCI_SIM_PROFILE_H

#include "uci_sim_spec.h"

typedef struct {
    const char* name;
    uint16_t uci_version;
    uint16_t mac_version;
    uint16_t phy_version;
    uint16_t test_version;
    uint8_t default_device_state;
    uint8_t default_low_power_mode;
    uint8_t default_device_pan_id[2];
    uint16_t default_session_max_data_size;
    uint32_t ranging_interval_ms;
    uint32_t ranging_event_period_ms;
    uint8_t core_caps_payload[UCI_SIM_MAX_PAYLOAD];
    uint16_t core_caps_payload_len;
} uci_sim_profile_t;

const uci_sim_profile_t* uci_sim_default_profile(void);

#endif
