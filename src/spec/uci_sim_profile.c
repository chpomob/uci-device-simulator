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
    .default_device_state = UCI_DEVICE_STATE_READY,
    .default_low_power_mode = 0x00,
    .default_device_pan_id = { 0x00, 0x00 },
    .initial_uwbs_timestamp = 0x1122334455667788ULL,
    .uwbs_timestamp_increment = 1ULL,
    .default_session_type = UCI_SESSION_TYPE_RANGING,
    .initial_session_state = UCI_SESSION_STATE_INIT,
    .session_status_reason_code = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
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
    .ranging_interval_ms = 1000U,
    .ranging_event_period_ms = 1000U,
    .ranging_stream_burst_count = 3,
    .core_caps_payload = { UCI_STATUS_OK, 0x01, 0xE4, 0x00 },
    .core_caps_payload_len = 4,
    .range_data_notification_oid = UCI_SESSION_START,
    .range_data_payload_template = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0xE8, 0x03, 0x00, 0x00,
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
        0x03,
        0x04,
        0x05,
        0x07,
        0x09,
        0x11
    },
    .supported_session_app_config_id_count = 7,
    .default_session_app_config_ids = {
        0x00,
        0x03,
        0x04,
        0x05,
        0x07,
        0x09,
        0x11
    },
    .default_session_app_config_value_lens = {
        1, 1, 1, 1, 2, 4, 1
    },
    .default_session_app_config_values = {
        { 0x01 },
        { 0x00 },
        { 0x09 },
        { 0x01 },
        { 0x34, 0x12 },
        { 0xE8, 0x03, 0x00, 0x00 },
        { 0x00 }
    },
    .default_session_app_config_count = 7,
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
    if (profile == NULL) {
        return 0;
    }

    return list_contains(profile->supported_session_app_config_ids,
                         profile->supported_session_app_config_id_count,
                         config_id);
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
