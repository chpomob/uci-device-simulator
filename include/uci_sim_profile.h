#ifndef UCI_SIM_PROFILE_H
#define UCI_SIM_PROFILE_H

#include "uci_sim_spec.h"

#define UCI_SIM_MAX_PROFILE_FEATURES 16U

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
    uint8_t supported_core_oids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_core_oid_count;
    uint8_t supported_session_config_oids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_session_config_oid_count;
    uint8_t supported_session_control_oids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_session_control_oid_count;
    uint8_t supported_core_config_ids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_core_config_id_count;
    uint8_t supported_session_app_config_ids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_session_app_config_id_count;
    uint8_t supported_notification_oids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_notification_oid_count;
} uci_sim_profile_t;

const uci_sim_profile_t* uci_sim_default_profile(void);
int uci_sim_profile_supports_command(const uci_sim_profile_t* profile, uint8_t gid, uint8_t oid);
int uci_sim_profile_supports_core_config(const uci_sim_profile_t* profile, uint8_t config_id);
int uci_sim_profile_supports_session_app_config(const uci_sim_profile_t* profile, uint8_t config_id);
int uci_sim_profile_supports_notification(const uci_sim_profile_t* profile, uint8_t oid);

#endif
