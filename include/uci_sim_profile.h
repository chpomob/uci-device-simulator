#ifndef UCI_SIM_PROFILE_H
#define UCI_SIM_PROFILE_H

#include "uci_sim_spec.h"

#define UCI_SIM_MAX_PROFILE_FEATURES 80U
#define UCI_SIM_MAX_RANGE_DATA_TEMPLATE 64U
#define UCI_SIM_MAX_SESSION_TRANSITIONS 4U
#define UCI_SIM_MAX_PROFILE_APP_CONFIG_LEN 32U

typedef struct {
    uint8_t oid;
    uint8_t allowed_states_mask;
    uint8_t next_state;
    uint8_t invalid_status;
} uci_sim_session_transition_t;

typedef enum {
    UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE = 0,
    UCI_SIM_INVALID_CONFIG_SURFACE_SESSION_STATUS = 1
} uci_sim_invalid_config_surface_t;

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
    uint16_t supported_min_ranging_interval_ms;
    uint8_t invalid_ranging_interval_status;
    uint8_t invalid_ranging_interval_reason_code;
    uint8_t invalid_ranging_interval_surface;
    uint8_t supported_result_report_config_mask;
    uint8_t invalid_result_report_config_status;
    uint8_t invalid_result_report_config_reason_code;
    uint8_t invalid_result_report_config_surface;
    uint8_t supported_sts_config_mask;
    uint8_t invalid_sts_config_status;
    uint8_t invalid_sts_config_reason_code;
    uint8_t invalid_sts_config_surface;
    uint8_t supported_aoa_result_req_max;
    uint8_t invalid_aoa_result_req_status;
    uint8_t invalid_aoa_result_req_reason_code;
    uint8_t invalid_aoa_result_req_surface;
    uint8_t supported_prf_mode_max;
    uint8_t invalid_prf_mode_status;
    uint8_t invalid_prf_mode_reason_code;
    uint8_t invalid_prf_mode_surface;
    uint8_t supported_bprf_preamble_code_index_min;
    uint8_t supported_bprf_preamble_code_index_max;
    uint8_t supported_hprf_preamble_code_index_min;
    uint8_t supported_hprf_preamble_code_index_max;
    uint8_t invalid_preamble_code_index_status;
    uint8_t invalid_preamble_code_index_reason_code;
    uint8_t invalid_preamble_code_index_surface;
    uint8_t supported_bprf_sfd_id_mask;
    uint8_t supported_hprf_sfd_id_mask;
    uint8_t invalid_sfd_id_status;
    uint8_t invalid_sfd_id_reason_code;
    uint8_t invalid_sfd_id_surface;
    uint8_t supported_psdu_data_rate_max;
    uint8_t invalid_psdu_data_rate_status;
    uint8_t invalid_psdu_data_rate_reason_code;
    uint8_t invalid_psdu_data_rate_surface;
    uint8_t supported_rssi_reporting_max;
    uint8_t invalid_rssi_reporting_status;
    uint8_t invalid_rssi_reporting_reason_code;
    uint8_t invalid_rssi_reporting_surface;
    uint16_t supported_ranging_round_usage_mask;
    uint8_t invalid_ranging_round_usage_status;
    uint8_t invalid_ranging_round_usage_reason_code;
    uint8_t invalid_ranging_round_usage_surface;
    uint8_t supported_multi_node_mode_mask;
    uint8_t invalid_multi_node_mode_status;
    uint8_t invalid_multi_node_mode_reason_code;
    uint8_t invalid_multi_node_mode_surface;
    uint16_t supported_channel_number_mask;
    uint8_t invalid_channel_number_status;
    uint8_t invalid_channel_number_reason_code;
    uint8_t invalid_channel_number_surface;
    uint8_t supported_max_controlees;
    uint8_t invalid_num_of_controlees_status;
    uint8_t invalid_num_of_controlees_reason_code;
    uint8_t invalid_num_of_controlees_surface;
    uint8_t supported_mac_address_mode_mask;
    uint8_t invalid_mac_address_mode_status;
    uint8_t invalid_mac_address_mode_reason_code;
    uint8_t invalid_mac_address_mode_surface;
    uint8_t invalid_device_mac_address_status;
    uint8_t invalid_device_mac_address_reason_code;
    uint8_t invalid_device_mac_address_surface;
    uint8_t invalid_dst_mac_address_status;
    uint8_t invalid_dst_mac_address_reason_code;
    uint8_t invalid_dst_mac_address_surface;
    uint8_t supported_device_type_mask;
    uint8_t invalid_device_type_status;
    uint8_t invalid_device_type_reason_code;
    uint8_t invalid_device_type_surface;
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
    uint8_t range_data_measurement_type_offset;
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
    uint8_t default_session_app_config_ids[UCI_SIM_MAX_PROFILE_FEATURES];
    uint8_t default_session_app_config_value_lens[UCI_SIM_MAX_PROFILE_FEATURES];
    uint8_t default_session_app_config_values[UCI_SIM_MAX_PROFILE_FEATURES][UCI_SIM_MAX_PROFILE_APP_CONFIG_LEN];
    size_t default_session_app_config_count;
    uint8_t supported_notification_oids[UCI_SIM_MAX_PROFILE_FEATURES];
    size_t supported_notification_oid_count;
} uci_sim_profile_t;

const uci_sim_profile_t* uci_sim_default_profile(void);
int uci_sim_profile_supports_command(const uci_sim_profile_t* profile, uint8_t gid, uint8_t oid);
int uci_sim_profile_supports_core_config(const uci_sim_profile_t* profile, uint8_t config_id);
int uci_sim_profile_supports_session_app_config(const uci_sim_profile_t* profile, uint8_t config_id);
int uci_sim_profile_supports_notification(const uci_sim_profile_t* profile, uint8_t oid);
int uci_sim_profile_supports_multicast_action(const uci_sim_profile_t* profile, uint8_t action);
const uci_sim_session_transition_t* uci_sim_profile_get_session_transition(const uci_sim_profile_t* profile,
                                                                           uint8_t oid);

#endif
