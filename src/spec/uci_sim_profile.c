#include "uci_sim_profile.h"

static int list_contains(const uint8_t* values, size_t count, uint8_t needle) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (values[i] == needle) {
            return 1;
        }
    }
    return 0;
}

static const uci_sim_profile_t k_default_profile = {
    .name = "default_qorvo_like",
    .uci_version = 0x0100,
    .mac_version = 0x0200,
    .phy_version = 0x0200,
    .test_version = 0x0100,
    /* Vendor-specific DEVICE_INFO data (qm-firmware format, 52 bytes minimum):
     * fw_major, fw_minor, fw_patch, fw_rc (4)
     * build_job u64 LE (8)
     * OEM version (4 — skipped by Cherry)
     * soc_id (16)
     * device_id u32 LE (4) = QM35825 (0x8BED)
     * package_id (1) = 0x01
     * flavor (24 bytes, optional)
     * product_id u32 LE (4, optional) — Cherry uses this in newer firmware
     * soi_variant u32 LE (4, optional) — Cherry needs this for calibration
     * rom_revision u16 LE (2, optional)
     * Total: 52 + 24 + 4 + 4 + 2 = 86 */
    .vendor_specific_length = 86,
    .vendor_specific_data = {
        0x01, 0x00, 0x00, 0x00,  /* fw_major=1, minor=0, patch=0, rc=0 */
        0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* build_job=66 */
        0x00, 0x00, 0x00, 0x00,  /* OEM version (skipped) */
        0x51, 0x4D, 0x33, 0x35, 0x38, 0x32, 0x35, 0x00,  /* soc_id: "QM35825\0..." */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* ...continued */
        0xED, 0x8B, 0x00, 0x00,  /* device_id = 0x8BED (QM35825) */
        0x01,                    /* package_id = SIP */
        /* flavor (24 bytes) */
        'Q','M','3','5','8','2','5','_','S','I','P','_','V','1','.','0',
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        /* product_id (4 bytes) */
        0xED, 0x8B, 0x00, 0x00,  /* 0x8BED */
        /* soi_variant (4 bytes) */
        0x02, 0x00, 0x00, 0x00,  /* SOI variant 2 */
        /* rom_revision (2 bytes) */
        0x01, 0x00,               /* ROM rev 1 */
    },
    .device_stats_temperature = 3500,  /* 35.00°C */
    .default_device_state = UCI_DEVICE_STATE_READY,
    .default_low_power_mode = 0x00,
    .default_device_pan_id = { 0x00, 0x00 },
    .initial_uwbs_timestamp = 0x1122334455667788ULL,
    .uwbs_timestamp_increment = 1ULL,
    .default_session_type = UCI_SESSION_TYPE_RANGING,
    .initial_session_state = UCI_SESSION_STATE_INIT,
    .session_status_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .supported_min_ranging_interval_ms = 50U,
    .invalid_ranging_interval_status = UCI_STATUS_INVALID_RANGE,
    .invalid_ranging_interval_reason_code = UCI_SESSION_REASON_ERROR_INVALID_RANGING_INTERVAL,
    .invalid_ranging_interval_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_min_slot_duration_rstu = 16U,
    .invalid_slot_duration_status = UCI_STATUS_INVALID_PARAM,
    .invalid_slot_duration_reason_code = UCI_SESSION_REASON_ERROR_SLOT_LENGTH_NOT_SUPPORTED,
    .invalid_slot_duration_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_result_report_config_mask = 0x0FU,
    .invalid_result_report_config_status = UCI_STATUS_INVALID_PARAM,
    .invalid_result_report_config_reason_code = UCI_SESSION_REASON_ERROR_INVALID_RESULT_REPORT_CONFIG,
    .invalid_result_report_config_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_sts_config_mask = 0x1FU,
    .invalid_sts_config_status = UCI_STATUS_INVALID_PARAM,
    .invalid_sts_config_reason_code = UCI_SESSION_REASON_ERROR_INVALID_STS_CONFIG,
    .invalid_sts_config_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_aoa_result_req_max = 0x03,
    .invalid_aoa_result_req_status = UCI_STATUS_INVALID_PARAM,
    .invalid_aoa_result_req_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_aoa_result_req_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_prf_mode_max = 0x02,
    .invalid_prf_mode_status = UCI_STATUS_INVALID_PARAM,
    .invalid_prf_mode_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_prf_mode_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_bprf_preamble_code_index_min = 9U,
    .supported_bprf_preamble_code_index_max = 12U,
    .supported_hprf_preamble_code_index_min = 25U,
    .supported_hprf_preamble_code_index_max = 32U,
    .invalid_preamble_code_index_status = UCI_STATUS_INVALID_PARAM,
    .invalid_preamble_code_index_reason_code = UCI_SESSION_REASON_ERROR_INVALID_PREAMBLE_CODE_INDEX,
    .invalid_preamble_code_index_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_bprf_sfd_id_mask = (uint8_t)((1U << 0) | (1U << 2)),
    .supported_hprf_sfd_id_mask = (uint8_t)((1U << 1) | (1U << 2) | (1U << 3) | (1U << 4)),
    .invalid_sfd_id_status = UCI_STATUS_INVALID_PARAM,
    .invalid_sfd_id_reason_code = UCI_SESSION_REASON_ERROR_INVALID_SFD_ID,
    .invalid_sfd_id_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_psdu_data_rate_max = 0x03,
    .invalid_psdu_data_rate_status = UCI_STATUS_INVALID_PARAM,
    .invalid_psdu_data_rate_reason_code = UCI_SESSION_REASON_ERROR_INVALID_PSDU_DATA_RATE,
    .invalid_psdu_data_rate_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_preamble_duration_max = 0x01,
    .invalid_preamble_duration_status = UCI_STATUS_INVALID_PARAM,
    .invalid_preamble_duration_reason_code = UCI_SESSION_REASON_ERROR_INVALID_PREAMBLE_DURATION,
    .invalid_preamble_duration_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_sts_length_max = 0x02,
    .invalid_sts_length_status = UCI_STATUS_INVALID_PARAM,
    .invalid_sts_length_reason_code = UCI_SESSION_REASON_ERROR_INVALID_STS_LENGTH,
    .invalid_sts_length_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_key_rotation_max = 0x01,
    .invalid_key_rotation_status = UCI_STATUS_INVALID_PARAM,
    .invalid_key_rotation_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_key_rotation_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_key_rotation_rate_max = 0x0F,
    .invalid_key_rotation_rate_status = UCI_STATUS_INVALID_PARAM,
    .invalid_key_rotation_rate_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_key_rotation_rate_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_number_of_sts_segments_max = 0x04,
    .invalid_number_of_sts_segments_status = UCI_STATUS_INVALID_PARAM,
    .invalid_number_of_sts_segments_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_number_of_sts_segments_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_link_layer_mode_max = 0x01,
    .invalid_link_layer_mode_status = UCI_STATUS_INVALID_PARAM,
    .invalid_link_layer_mode_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_link_layer_mode_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_ranging_time_struct_mask = (uint8_t)((1U << 0) | (1U << 1)),
    .invalid_ranging_time_struct_status = UCI_STATUS_INVALID_PARAM,
    .invalid_ranging_time_struct_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_ranging_time_struct_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_scheduled_mode_mask = (uint8_t)(1U << UCI_SCHEDULED_MODE_TIME_SCHEDULED),
    .invalid_scheduled_mode_status = UCI_STATUS_INVALID_PARAM,
    .invalid_scheduled_mode_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_scheduled_mode_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_max_block_stride_length = 0xFF,
    .invalid_block_stride_length_status = UCI_STATUS_INVALID_PARAM,
    .invalid_block_stride_length_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_block_stride_length_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_min_slots_per_rr = 1U,
    .supported_max_slots_per_rr = 0xFFU,
    .invalid_slots_per_rr_status = UCI_STATUS_INVALID_PARAM,
    .invalid_slots_per_rr_reason_code = UCI_SESSION_REASON_ERROR_INSUFFICIENT_SLOTS_PER_RR,
    .invalid_slots_per_rr_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .invalid_cap_size_range_status = UCI_STATUS_INVALID_PARAM,
    .invalid_cap_size_range_reason_code = UCI_SESSION_REASON_ERROR_INVALID_CAP_SIZE_RANGE,
    .invalid_cap_size_range_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_rssi_reporting_max = 0x01,
    .invalid_rssi_reporting_status = UCI_STATUS_INVALID_PARAM,
    .invalid_rssi_reporting_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_rssi_reporting_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_ranging_round_usage_mask =
        (uint16_t)((1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) | (1U << 7) | (1U << 8)),
    .invalid_ranging_round_usage_status = UCI_STATUS_INVALID_PARAM,
    .invalid_ranging_round_usage_reason_code = UCI_SESSION_REASON_ERROR_INVALID_RANGING_ROUND_USAGE,
    .invalid_ranging_round_usage_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_multi_node_mode_mask = (uint8_t)((1U << 0) | (1U << 1) | (1U << 2)),
    .invalid_multi_node_mode_status = UCI_STATUS_INVALID_PARAM,
    .invalid_multi_node_mode_reason_code = UCI_SESSION_REASON_ERROR_INVALID_MULTI_NODE_MODE,
    .invalid_multi_node_mode_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_channel_number_mask = (uint16_t)((1U << 5) | (1U << 9)),
    .invalid_channel_number_status = UCI_STATUS_INVALID_PARAM,
    .invalid_channel_number_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_channel_number_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_max_controlees = 8,
    .invalid_num_of_controlees_status = UCI_STATUS_INVALID_PARAM,
    .invalid_num_of_controlees_reason_code = UCI_SESSION_REASON_ERROR_INVALID_NUM_OF_CONTROLEES,
    .invalid_num_of_controlees_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_mac_address_mode_mask = (uint8_t)(1U << UCI_MAC_ADDRESS_MODE_SHORT),
    .invalid_mac_address_mode_status = UCI_STATUS_INVALID_PARAM,
    .invalid_mac_address_mode_reason_code = UCI_SESSION_REASON_ERROR_MAC_ADDRESS_MODE_NOT_SUPPORTED,
    .invalid_mac_address_mode_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .invalid_device_mac_address_status = UCI_STATUS_INVALID_PARAM,
    .invalid_device_mac_address_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_device_mac_address_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .invalid_dst_mac_address_status = UCI_STATUS_INVALID_PARAM,
    .invalid_dst_mac_address_reason_code = UCI_SESSION_REASON_ERROR_INVALID_DST_ADDRESS_LIST,
    .invalid_dst_mac_address_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .supported_device_type_mask =
        (uint8_t)((1U << UCI_DEVICE_TYPE_CONTROLEE) | (1U << UCI_DEVICE_TYPE_CONTROLLER)),
    .invalid_device_type_status = UCI_STATUS_INVALID_PARAM,
    .invalid_device_type_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
    .invalid_device_type_surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE,
    .session_transitions = {
        {
            .oid = UCI_SESSION_START,
            .allowed_states_mask = (1U << UCI_SESSION_STATE_INIT) | (1U << UCI_SESSION_STATE_IDLE),
            .next_state = UCI_SESSION_STATE_ACTIVE,
            .invalid_status = UCI_STATUS_REJECTED,
        },
        {
            .oid = UCI_SESSION_STOP,
            .allowed_states_mask = (1U << UCI_SESSION_STATE_ACTIVE),
            .next_state = UCI_SESSION_STATE_IDLE,
            .invalid_status = UCI_STATUS_REJECTED,
        }
    },
    .session_transition_count = 2,
    .default_session_max_data_size = 0x0200,
    .ranging_interval_ms = 2000U,
    .ranging_event_period_ms = 2000U,
    .ranging_stream_burst_count = 3,
    .core_caps_payload = { UCI_STATUS_OK, 0x01, 0xE4, 0x00 },
    .core_caps_payload_len = 4,
    .range_data_notification_oid = UCI_SESSION_START,
    .range_data_payload_template = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0xD0, 0x07, 0x00, 0x00,
        0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01,
        0x12, 0x34, 0x00, 0x00,
        0x64, 0x00,
        0x14, 0x00, 0x08, 0x05, 0x00, 0x07, 0x10, 0x00, 0x06, 0x03, 0x00, 0x09, 0x02, 0xE0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    },
    .range_data_payload_len = 52,
    .range_data_sequence_offset = 0,
    .range_data_primary_session_id_offset = 4,
    .range_data_secondary_session_id_offset = 16,
    .range_data_interval_offset = 9,
    .range_data_measurement_type_offset = 13,
    .range_data_measurement_distance_offset = 29,
    .range_data_distance_base_cm = 100,
    .range_data_distance_step_cm = 5,
    .supported_core_oids = {
        UCI_CORE_DEVICE_RESET,
        UCI_CORE_DEVICE_INFO,
        UCI_CORE_GET_CAPS_INFO,
        UCI_CORE_SET_CONFIG,
        UCI_CORE_GET_CONFIG,
        UCI_CORE_QUERY_UWBS_TIMESTAMP
    },
    .supported_core_oid_count = 6,
    .supported_session_config_oids = {
        UCI_SESSION_INIT,
        UCI_SESSION_DEINIT,
        UCI_SESSION_SET_APP_CONFIG,
        UCI_SESSION_GET_APP_CONFIG,
        UCI_SESSION_GET_COUNT,
        UCI_SESSION_GET_STATE,
        UCI_SESSION_UPDATE_CONTROLLER_MULTICAST_LIST,
        UCI_SESSION_UPDATE_DT_ANCHOR_RANGING_ROUNDS,
        UCI_SESSION_UPDATE_DT_TAG_RANGING_ROUNDS,
        UCI_SESSION_DATA_TRANSFER_PHASE_CONFIG,
        UCI_SESSION_QUERY_DATA_SIZE_IN_RANGING,
        UCI_SESSION_SET_HUS_CONTROLLER_CONFIG,
        UCI_SESSION_SET_HUS_CONTROLEE_CONFIG
    },
    .supported_session_config_oid_count = 13,
    .supported_session_control_oids = {
        UCI_SESSION_START,
        UCI_SESSION_STOP,
        UCI_SESSION_GET_RANGING_COUNT,
        UCI_SESSION_LOGICAL_LINK_CREATE,
        UCI_SESSION_LOGICAL_LINK_CLOSE,
        UCI_SESSION_LOGICAL_LINK_GET_PARAM
    },
    .supported_session_control_oid_count = 6,
    .supported_core_config_ids = {
        UCI_DEVICE_CONFIG_DEVICE_STATE,
        UCI_DEVICE_CONFIG_LOW_POWER_MODE,
        UCI_DEVICE_CONFIG_DEVICE_PAN_ID
    },
    .supported_core_config_id_count = 3,
    .supported_session_app_config_ids = {
        0x00,
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0A,
        0x0B,
        0x0C,
        0x0D,
        0x0E,
        0x0F,
        0x10,
        0x11,
        0x12,
        0x13,
        0x14,
        0x15,
        0x16,
        0x17,
        0x18,
        0x19,
        0x1A,
        0x1B,
        0x1C,
        0x1D,
        0x1E,
        0x1F,
        0x20,
        0x21,
        0x22,
        0x23,
        0x24,
        0x25,
        0x26,
        0x27,
        0x28,
        0x29,
        0x2A,
        0x2B,
        0x2C,
        0x2D,
        0x2E,
        0x2F,
        0x30,
        0x31,
        0x32,
        0x33,
        0x34,
        0x35,
        0x36,
        0x37,
        0x38,
        0x39,
        0x3A,
        0x3B,
        0x3C,
        0x3D,
        0x3E,
        0x3F,
        0x40,
        0x41,
        0x42,
        0x43,
        0x44,
        0x45,
        0x46,
        0x47,
        0x48,
        0x49,
        0x4A,
        0x4B,
        0x4C,
        0x4D,
        /* Extended FiRa range for Cherry SDK compat */
        0xB6,  /* ANTENNA_SET_ID */
        0xE6,  /* RX_ANTENNA_SELECTION */
        0xE7,  /* TX_ANTENNA_SELECTION */
    },
    .supported_session_app_config_id_count = 81,
    .default_session_app_config_ids = {
        0x00,
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0A,
        0x0B,
        0x0C,
        0x0D,
        0x0E,
        0x0F,
        0x10,
        0x11,
        0x12,
        0x13,
        0x14,
        0x15,
        0x16,
        0x17,
        0x18,
        0x19,
        0x1A,
        0x1B,
        0x1C,
        0x1D,
        0x1E,
        0x1F,
        0x20,
        0x21,
        0x22,
        0x23,
        0x24,
        0x25,
        0x26,
        0x27,
        0x28,
        0x29,
        0x2A,
        0x2B,
        0x2C,
        0x2D,
        0x2E,
        0x2F,
        0x30,
        0x31,
        0x32,
        0x33,
        0x34,
        0x35,
        0x36,
        0x37,
        0x38,
        0x39,
        0x3A,
        0x3B,
        0x3C,
        0x3D,
        0x3E,
        0x3F,
        0x40,
        0x41,
        0x42,
        0x43,
        0x44,
        0x45,
        0x46,
        0x47,
        0x48,
        0x49,
        0x4A,
        0x4B,
        0x4C,
        0x4D
    },
    .default_session_app_config_value_lens = {
        1, 1, 1, 1, 1, 1, 2, 6, 2, 4, 4, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 2, 1, 1, 2, 1, 1, 2, 8, 1, 2, 4, 1, 1, 1, 1, 4, 1, 2, 4, 4, 1, 1, 3, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        16, 16, 1, 9, 1, 1, 1, 1, 1
    },
    .default_session_app_config_values = {
        { 0x01 },
        { 0x01 },
        { 0x01 },
        { 0x01 },
        { 0x05 },
        { 0x03 },
        { 0xCD, 0xAB },
        { 0x78, 0x56, 0x89, 0x67, 0x9A, 0x78 },
        { 0x60, 0x09 },
        { 0xD0, 0x07, 0x00, 0x00 },
        { 0x05, 0x00, 0x00, 0x00 },
        { 0x01 },
        { 0x05 },
        { 0x03 },
        { 0x01 },
        { 0x64, 0x00 },
        { 0xF4, 0x01 },
        { 0x01 },
        { 0x02 },
        { 0x01 },
        { 0x0C },
        { 0x02 },
        { 0x01 },
        { 0x01 },
        { 0x01 },
        { 0x00 },
        { 0x00 },
        { 0x06 },
        { 0x01 },
        { 0x2D, 0x00 },
        { 0x00 },
        { 0x00 },
        { 0x00, 0x00 },
        { 0x10, 0x00 },
        { 0x01 },
        { 0x01 },
        { 0x0F, 0x00 },
        { 0x4B },
        { 0x00 },
        { 0x34, 0x12 },
        { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 },
        { 0x02 },
        { 0x00, 0x00 },
        { 0xE8, 0x03, 0x00, 0x00 },
        { 0x01 },
        { 0x00 },
        { 0x07 },
        { 0x04 },
        { 0x78, 0x56, 0x34, 0x12 },
        { 0x01 },
        { 0x10, 0x00 },
        { 0x64, 0x00, 0x00, 0x00 },
        { 0xFA, 0x00, 0x00, 0x00 },
        { 0x02 },
        { 0x03 },
        { 0x01, 0x02, 0x03 },
        { 0x07 },
        { 0x01 },
        { 0x02 },
        { 0x00, 0x04 },
        { 0x05 },
        { 0x01 },
        { 0x03 },
        { 0x01 },
        { 0x01 },
        { 0x00 },
        { 0x01 },
        { 0x01 },
        { 0x01 },
        { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
          0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
        { 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
          0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 },
        { 0x01 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x01 },
        { 0x02 },
        { 0x03 },
        { 0x01 },
        { 0x0A }
    },
    .default_session_app_config_count = 78,
    .supported_notification_oids = {
        UCI_CORE_DEVICE_STATUS_NTF,
        UCI_SESSION_STATUS_NTF,
        UCI_SESSION_START,
        UCI_SESSION_DATA_CREDIT_NTF,
        UCI_SESSION_DATA_TRANSFER_STATUS_NTF,
        UCI_SESSION_LOGICAL_LINK_UWBS_CREATE,
        UCI_SESSION_LOGICAL_LINK_UWBS_CLOSE
    },
    .supported_notification_oid_count = 7
};

const uci_sim_profile_t* uci_sim_default_profile(void) {
    return &k_default_profile;
}

int uci_sim_profile_supports_command(const uci_sim_profile_t* profile, uint8_t gid, uint8_t oid) {
    if (profile == NULL) {
        return 0;
    }

    switch (gid) {
        case UCI_GID_CORE:
            return list_contains(profile->supported_core_oids, profile->supported_core_oid_count, oid);
        case UCI_GID_SESSION_CONFIG:
            return list_contains(profile->supported_session_config_oids, profile->supported_session_config_oid_count, oid);
        case UCI_GID_SESSION_CONTROL:
            return list_contains(profile->supported_session_control_oids, profile->supported_session_control_oid_count, oid);
        default:
            return 0;
    }
}

int uci_sim_profile_supports_core_config(const uci_sim_profile_t* profile, uint8_t config_id) {
    if (profile == NULL) {
        return 0;
    }

    return list_contains(profile->supported_core_config_ids,
                         profile->supported_core_config_id_count,
                         config_id);
}

int uci_sim_profile_supports_session_app_config(const uci_sim_profile_t* profile, uint8_t config_id) {
    (void)profile;
    (void)config_id;
    /* Accept all app config IDs — the simulator stores them faithfully.
     * Validation for specific parameters happens in uci_sim_validate_session_app_config(). */
    return 1;
}

int uci_sim_profile_supports_notification(const uci_sim_profile_t* profile, uint8_t oid) {
    if (profile == NULL) {
        return 0;
    }

    return list_contains(profile->supported_notification_oids,
                         profile->supported_notification_oid_count,
                         oid);
}

int uci_sim_profile_supports_multicast_action(const uci_sim_profile_t* profile, uint8_t action) {
    (void)profile;

    switch (action) {
        case UCI_MULTICAST_ACTION_ADD:
        case UCI_MULTICAST_ACTION_REMOVE:
        case UCI_MULTICAST_ACTION_ADD_SHORT_KEY:
        case UCI_MULTICAST_ACTION_ADD_LONG_KEY:
            return 1;
        default:
            return 0;
    }
}

const uci_sim_session_transition_t* uci_sim_profile_get_session_transition(const uci_sim_profile_t* profile,
                                                                           uint8_t oid) {
    size_t i;

    if (profile == NULL) {
        return NULL;
    }

    for (i = 0; i < profile->session_transition_count; ++i) {
        if (profile->session_transitions[i].oid == oid) {
            return &profile->session_transitions[i];
        }
    }

    return NULL;
}
