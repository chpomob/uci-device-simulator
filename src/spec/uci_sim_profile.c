#include "uci_sim_profile.h"

static const uci_sim_profile_t k_default_profile = {
    .name = "default_qorvo_like",
    .uci_version = 0x0100,
    .mac_version = 0x0200,
    .phy_version = 0x0200,
    .test_version = 0x0100,
    .default_device_state = UCI_DEVICE_STATE_READY,
    .default_low_power_mode = 0x00,
    .default_device_pan_id = { 0x00, 0x00 },
    .default_session_max_data_size = 0x0200,
    .ranging_interval_ms = 1000U,
    .ranging_event_period_ms = 1000U,
    .core_caps_payload = { UCI_STATUS_OK, 0x01, 0xE4, 0x00 },
    .core_caps_payload_len = 4
};

const uci_sim_profile_t* uci_sim_default_profile(void) {
    return &k_default_profile;
}
