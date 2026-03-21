#include "uci_sim_validation.h"

static uint32_t read_u32_le(const uint8_t* payload) {
    return (uint32_t)payload[0] |
           ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) |
           ((uint32_t)payload[3] << 24);
}

static void set_invalid_result(uci_sim_validation_result_t* result,
                               uint8_t status,
                               uint8_t reason,
                               uint8_t surface) {
    if (!result) {
        return;
    }

    result->ok = 0;
    result->status = status;
    result->reason = reason;
    result->surface = surface;
}

static int validate_ranging_interval_ms(const uci_sim_profile_t* profile,
                                        uint32_t interval_ms,
                                        uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (interval_ms < profile->supported_min_ranging_interval_ms) {
        set_invalid_result(result,
                           profile->invalid_ranging_interval_status,
                           profile->invalid_ranging_interval_reason_code,
                           profile->invalid_ranging_interval_surface);
        return -1;
    }

    return 0;
}

void uci_sim_validation_result_init(uci_sim_validation_result_t* result) {
    if (!result) {
        return;
    }

    result->ok = 1;
    result->status = UCI_STATUS_OK;
    result->reason = UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS;
    result->surface = UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE;
}

int uci_sim_validate_session_app_config(const uci_sim_profile_t* profile,
                                        const uci_sim_session_t* session,
                                        uint8_t config_id,
                                        const uint8_t* value,
                                        uint8_t value_len,
                                        uci_sim_validation_result_t* result) {
    (void)session;

    uci_sim_validation_result_init(result);

    if (config_id != UCI_APP_CONFIG_RANGING_INTERVAL) {
        return 0;
    }

    if (!value || value_len != 4) {
        set_invalid_result(result,
                           UCI_STATUS_INVALID_PARAM,
                           UCI_SESSION_REASON_ERROR_INVALID_RANGING_INTERVAL,
                           UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }

    return validate_ranging_interval_ms(profile, read_u32_le(value), result);
}

int uci_sim_validate_session_start(const uci_sim_profile_t* profile,
                                   const uci_sim_session_t* session,
                                   uci_sim_validation_result_t* result) {
    uint32_t interval_ms;

    uci_sim_validation_result_init(result);
    if (!session) {
        return 0;
    }

    interval_ms = uci_sim_session_get_ranging_interval_ms(session, profile);
    return validate_ranging_interval_ms(profile, interval_ms, result);
}
