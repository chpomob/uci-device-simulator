#ifndef UCI_SIM_PROFILE_H
#define UCI_SIM_PROFILE_H

#include "uci_sim_spec.h"

#define UCI_SIM_MAX_PROFILE_FEATURES 16U
#define UCI_SIM_MAX_RANGE_DATA_TEMPLATE 64U
#define UCI_SIM_MAX_SESSION_TRANSITIONS 4U

typedef struct {
    uint8_t oid;
    uint8_t allowed_states_mask;
    uint8_t next_state;
    uint8_t invalid_status;
} uci_sim_session_transition_t;

typedef struct {
    const char* name;
    uint16_t uci_version;
    uint16_t mac_version;
    uint16_t phy_version;
    uint16_t test_version;
    uint8_t default_device_state;
    uint8_t default_low_power_mode;
    uint8_t default_device_pan_id[2];
    uint64_t initial_uwbs_timestamp;
    uint64_t uwbs_timestamp_increment;
    uint8_t default_session_type;
    uint8_t initial_session_state;
    uint8_t session_status_reason_code;
    uci_sim_session_transition_t session_transitions[UCI_SIM_MAX_SESSION_TRANSITIONS];
    size_t session_transition_count;
    uint16_t default_session_max_data_size;
    uint32_t ranging_interval_ms;
    uint32_t ranging_event_period_ms;
    uint8_t ranging_stream_burst_count;
    uint8_t core_caps_payload[UCI_SIM_MAX_PAYLOAD];
    uint16_t core_caps_payload_len;
    uint8_t range_data_notification_oid;
    uint8_t range_data_payload_template[UCI_SIM_MAX_RANGE_DATA_TEMPLATE];
    uint16_t range_data_payload_len;
    uint8_t range_data_sequence_offset;
    uint8_t range_data_primary_session_id_offset;
    uint8_t range_data_secondary_session_id_offset;
    uint8_t range_data_interval_offset;
    uint8_t range_data_measurement_distance_offset;
    uint16_t range_data_distance_base_cm;
    uint16_t range_data_distance_step_cm;
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
const uci_sim_session_transition_t* uci_sim_profile_get_session_transition(const uci_sim_profile_t* profile,
                                                                           uint8_t oid);

#endif
