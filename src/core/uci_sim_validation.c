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

static int validate_result_report_config(const uci_sim_profile_t* profile,
                                         uint8_t result_report_config,
                                         uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if ((result_report_config & (uint8_t)~profile->supported_result_report_config_mask) != 0U) {
        set_invalid_result(result,
                           profile->invalid_result_report_config_status,
                           profile->invalid_result_report_config_reason_code,
                           profile->invalid_result_report_config_surface);
        return -1;
    }

    return 0;
}

static int validate_aoa_result_req(const uci_sim_profile_t* profile,
                                   uint8_t aoa_result_req,
                                   uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (aoa_result_req > profile->supported_aoa_result_req_max) {
        set_invalid_result(result,
                           profile->invalid_aoa_result_req_status,
                           profile->invalid_aoa_result_req_reason_code,
                           profile->invalid_aoa_result_req_surface);
        return -1;
    }

    return 0;
}

static int validate_rssi_reporting(const uci_sim_profile_t* profile,
                                   uint8_t rssi_reporting,
                                   uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (rssi_reporting > profile->supported_rssi_reporting_max) {
        set_invalid_result(result,
                           profile->invalid_rssi_reporting_status,
                           profile->invalid_rssi_reporting_reason_code,
                           profile->invalid_rssi_reporting_surface);
        return -1;
    }

    return 0;
}

static int validate_ranging_round_usage(const uci_sim_profile_t* profile,
                                        uint8_t ranging_round_usage,
                                        uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (ranging_round_usage >= 16U ||
        (profile->supported_ranging_round_usage_mask & (uint16_t)(1U << ranging_round_usage)) == 0U) {
        set_invalid_result(result,
                           profile->invalid_ranging_round_usage_status,
                           profile->invalid_ranging_round_usage_reason_code,
                           profile->invalid_ranging_round_usage_surface);
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
        if (config_id == UCI_APP_CONFIG_RESULT_REPORT_CONFIG) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   UCI_SESSION_REASON_ERROR_INVALID_RESULT_REPORT_CONFIG,
                                   UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_result_report_config(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_AOA_RESULT_REQ) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_aoa_result_req_reason_code
                                           : UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
                                   profile ? profile->invalid_aoa_result_req_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_aoa_result_req(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_RSSI_REPORTING) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_rssi_reporting_reason_code
                                           : UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
                                   profile ? profile->invalid_rssi_reporting_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_rssi_reporting(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_RANGING_ROUND_USAGE) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_ranging_round_usage_reason_code
                                           : UCI_SESSION_REASON_ERROR_INVALID_RANGING_ROUND_USAGE,
                                   profile ? profile->invalid_ranging_round_usage_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_ranging_round_usage(profile, value[0], result);
        }
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
    uint8_t result_report_config;
    uint8_t aoa_result_req;
    uint8_t rssi_reporting;
    uint8_t ranging_round_usage;

    uci_sim_validation_result_init(result);
    if (!session) {
        return 0;
    }

    interval_ms = uci_sim_session_get_ranging_interval_ms(session, profile);
    if (validate_ranging_interval_ms(profile, interval_ms, result) != 0) {
        return -1;
    }

    result_report_config = uci_sim_session_get_result_report_config(session);
    if (validate_result_report_config(profile, result_report_config, result) != 0) {
        return -1;
    }

    aoa_result_req = uci_sim_session_get_aoa_result_req(session);
    if (validate_aoa_result_req(profile, aoa_result_req, result) != 0) {
        return -1;
    }

    rssi_reporting = uci_sim_session_get_rssi_reporting(session);
    if (validate_rssi_reporting(profile, rssi_reporting, result) != 0) {
        return -1;
    }

    ranging_round_usage = uci_sim_session_get_ranging_round_usage(session);
    return validate_ranging_round_usage(profile, ranging_round_usage, result);
}
