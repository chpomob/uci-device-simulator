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
    .default_session_max_data_size = 0x0200,
    .ranging_interval_ms = 1000U,
    .ranging_event_period_ms = 1000U,
    .core_caps_payload = { UCI_STATUS_OK, 0x01, 0xE4, 0x00 },
    .core_caps_payload_len = 4,
    .supported_core_oids = {
        UCI_CORE_DEVICE_INFO,
        UCI_CORE_GET_CAPS_INFO,
        UCI_CORE_SET_CONFIG,
        UCI_CORE_GET_CONFIG
    },
    .supported_core_oid_count = 4,
    .supported_session_config_oids = {
        UCI_SESSION_INIT,
        UCI_SESSION_DEINIT,
        UCI_SESSION_SET_APP_CONFIG,
        UCI_SESSION_GET_APP_CONFIG,
        UCI_SESSION_GET_COUNT,
        UCI_SESSION_GET_STATE,
        UCI_SESSION_QUERY_DATA_SIZE_IN_RANGING
    },
    .supported_session_config_oid_count = 7,
    .supported_session_control_oids = {
        UCI_SESSION_START,
        UCI_SESSION_STOP,
        UCI_SESSION_GET_RANGING_COUNT
    },
    .supported_session_control_oid_count = 3,
    .supported_core_config_ids = {
        UCI_DEVICE_CONFIG_DEVICE_STATE,
        UCI_DEVICE_CONFIG_LOW_POWER_MODE,
        UCI_DEVICE_CONFIG_DEVICE_PAN_ID
    },
    .supported_core_config_id_count = 3,
    .supported_session_app_config_ids = {
        0x00
    },
    .supported_session_app_config_id_count = 1,
    .supported_notification_oids = {
        UCI_SESSION_STATUS_NTF,
        UCI_SESSION_START
    },
    .supported_notification_oid_count = 2
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
