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

static int validate_sts_config(const uci_sim_profile_t* profile,
                               uint8_t sts_config,
                               uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (sts_config >= 8U ||
        (profile->supported_sts_config_mask & (uint8_t)(1U << sts_config)) == 0U) {
        set_invalid_result(result,
                           profile->invalid_sts_config_status,
                           profile->invalid_sts_config_reason_code,
                           profile->invalid_sts_config_surface);
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

static int validate_device_type(const uci_sim_profile_t* profile,
                                uint8_t device_type,
                                uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (device_type >= 8U ||
        (profile->supported_device_type_mask & (uint8_t)(1U << device_type)) == 0U) {
        set_invalid_result(result,
                           profile->invalid_device_type_status,
                           profile->invalid_device_type_reason_code,
                           profile->invalid_device_type_surface);
        return -1;
    }

    return 0;
}

static int validate_number_of_controlees(const uci_sim_profile_t* profile,
                                        uint8_t number_of_controlees,
                                        uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (number_of_controlees > profile->supported_max_controlees) {
        set_invalid_result(result,
                           profile->invalid_num_of_controlees_status,
                           profile->invalid_num_of_controlees_reason_code,
                           profile->invalid_num_of_controlees_surface);
        return -1;
    }

    return 0;
}

static int validate_mac_address_mode(const uci_sim_profile_t* profile,
                                     uint8_t mac_address_mode,
                                     uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (mac_address_mode >= 8U ||
        (profile->supported_mac_address_mode_mask & (uint8_t)(1U << mac_address_mode)) == 0U) {
        set_invalid_result(result,
                           profile->invalid_mac_address_mode_status,
                           profile->invalid_mac_address_mode_reason_code,
                           profile->invalid_mac_address_mode_surface);
        return -1;
    }

    return 0;
}

static int validate_device_mac_address(const uci_sim_profile_t* profile,
                                       const uint8_t* value,
                                       uint8_t value_len,
                                       uint8_t mac_address_mode,
                                       uci_sim_validation_result_t* result) {
    uint8_t status = profile ? profile->invalid_device_mac_address_status : UCI_STATUS_INVALID_PARAM;
    uint8_t reason = profile ? profile->invalid_device_mac_address_reason_code
                             : UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS;
    uint8_t surface = profile ? profile->invalid_device_mac_address_surface
                              : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE;
    uint8_t expected_len = 0U;

    switch (mac_address_mode) {
        case UCI_MAC_ADDRESS_MODE_SHORT:
            expected_len = 2U;
            break;
        case UCI_MAC_ADDRESS_MODE_EXTENDED:
            expected_len = 8U;
            break;
        default:
            return validate_mac_address_mode(profile, mac_address_mode, result);
    }

    if (!value || value_len != expected_len) {
        set_invalid_result(result, status, reason, surface);
        return -1;
    }

    return 0;
}

static int validate_dst_mac_address(const uci_sim_profile_t* profile,
                                    const uint8_t* value,
                                    uint8_t value_len,
                                    uci_sim_validation_result_t* result) {
    uint8_t max_controlees = profile ? profile->supported_max_controlees : 8U;
    uint8_t status = profile ? profile->invalid_dst_mac_address_status : UCI_STATUS_INVALID_PARAM;
    uint8_t reason = profile ? profile->invalid_dst_mac_address_reason_code
                             : UCI_SESSION_REASON_ERROR_INVALID_DST_ADDRESS_LIST;
    uint8_t surface = profile ? profile->invalid_dst_mac_address_surface
                              : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE;

    if (!value || value_len == 0U || (value_len % 2U) != 0U || (value_len / 2U) > max_controlees) {
        set_invalid_result(result, status, reason, surface);
        return -1;
    }

    return 0;
}

static int validate_multi_node_mode(const uci_sim_profile_t* profile,
                                    uint8_t multi_node_mode,
                                    uci_sim_validation_result_t* result) {
    if (!profile) {
        return 0;
    }

    if (multi_node_mode >= 8U ||
        (profile->supported_multi_node_mode_mask & (uint8_t)(1U << multi_node_mode)) == 0U) {
        set_invalid_result(result,
                           profile->invalid_multi_node_mode_status,
                           profile->invalid_multi_node_mode_reason_code,
                           profile->invalid_multi_node_mode_surface);
        return -1;
    }

    return 0;
}

static int get_session_u8_config(const uci_sim_session_t* session,
                                 uint8_t config_id,
                                 uint8_t* value,
                                 uci_sim_validation_result_t* result,
                                 uint8_t status,
                                 uint8_t reason,
                                 uint8_t surface) {
    uint8_t value_len = 0;

    if (!session || !value) {
        return -1;
    }

    if (uci_sim_session_get_config(session, config_id, value, &value_len) != 0 || value_len != 1U) {
        set_invalid_result(result, status, reason, surface);
        return -1;
    }

    return 0;
}

static int validate_device_type_role_pair(const uci_sim_profile_t* profile,
                                          const uci_sim_session_t* session,
                                          uci_sim_validation_result_t* result) {
    uint8_t device_type = 0U;
    uint8_t device_role = 0U;
    uint8_t status = profile ? profile->invalid_device_type_status : UCI_STATUS_INVALID_PARAM;
    uint8_t reason = profile ? profile->invalid_device_type_reason_code
                             : UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS;
    uint8_t surface = profile ? profile->invalid_device_type_surface
                              : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE;

    if (get_session_u8_config(session, UCI_APP_CONFIG_DEVICE_TYPE, &device_type,
                              result, status, reason, surface) != 0) {
        return -1;
    }
    if (validate_device_type(profile, device_type, result) != 0) {
        return -1;
    }
    if (get_session_u8_config(session, UCI_APP_CONFIG_DEVICE_ROLE, &device_role,
                              result, UCI_STATUS_INVALID_PARAM,
                              UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
                              UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE) != 0) {
        return -1;
    }

    switch (device_role) {
        case UCI_DEVICE_ROLE_RESPONDER:
            if (device_type != UCI_DEVICE_TYPE_CONTROLEE) {
                set_invalid_result(result, status, reason, surface);
                return -1;
            }
            break;
        case UCI_DEVICE_ROLE_INITIATOR:
            if (device_type != UCI_DEVICE_TYPE_CONTROLLER) {
                set_invalid_result(result, status, reason, surface);
                return -1;
            }
            break;
        default:
            break;
    }

    return 0;
}

static int validate_multi_node_mode_topology(const uci_sim_profile_t* profile,
                                             const uci_sim_session_t* session,
                                             uci_sim_validation_result_t* result) {
    uint8_t multi_node_mode = 0U;
    uint8_t mac_address_mode = 0U;
    uint8_t number_of_controlees = 0U;
    uint8_t device_mac_value[UCI_SIM_MAX_CONFIG_VALUE] = {0};
    uint8_t device_mac_value_len = 0U;
    uint8_t dst_mac_value[UCI_SIM_MAX_CONFIG_VALUE] = {0};
    uint8_t dst_mac_value_len = 0U;
    uint8_t status = profile ? profile->invalid_multi_node_mode_status : UCI_STATUS_INVALID_PARAM;
    uint8_t reason = profile ? profile->invalid_multi_node_mode_reason_code
                             : UCI_SESSION_REASON_ERROR_INVALID_MULTI_NODE_MODE;
    uint8_t surface = profile ? profile->invalid_multi_node_mode_surface
                              : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE;

    if (get_session_u8_config(session, UCI_APP_CONFIG_MULTI_NODE_MODE, &multi_node_mode,
                              result, status, reason, surface) != 0) {
        return -1;
    }
    if (validate_multi_node_mode(profile, multi_node_mode, result) != 0) {
        return -1;
    }
    if (get_session_u8_config(session, UCI_APP_CONFIG_MAC_ADDRESS_MODE, &mac_address_mode,
                              result,
                              profile ? profile->invalid_mac_address_mode_status : UCI_STATUS_INVALID_PARAM,
                              profile ? profile->invalid_mac_address_mode_reason_code
                                      : UCI_SESSION_REASON_ERROR_MAC_ADDRESS_MODE_NOT_SUPPORTED,
                              profile ? profile->invalid_mac_address_mode_surface
                                      : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE) != 0) {
        return -1;
    }
    if (validate_mac_address_mode(profile, mac_address_mode, result) != 0) {
        return -1;
    }

    if (get_session_u8_config(session, UCI_APP_CONFIG_NUMBER_OF_CONTROLEES, &number_of_controlees,
                              result,
                              profile ? profile->invalid_num_of_controlees_status : UCI_STATUS_INVALID_PARAM,
                              profile ? profile->invalid_num_of_controlees_reason_code
                                      : UCI_SESSION_REASON_ERROR_INVALID_NUM_OF_CONTROLEES,
                              profile ? profile->invalid_num_of_controlees_surface
                                      : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE) != 0) {
        return -1;
    }
    if (validate_number_of_controlees(profile, number_of_controlees, result) != 0) {
        return -1;
    }
    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_DEVICE_MAC_ADDRESS, device_mac_value, &device_mac_value_len) != 0) {
        set_invalid_result(result,
                           profile ? profile->invalid_device_mac_address_status : UCI_STATUS_INVALID_PARAM,
                           profile ? profile->invalid_device_mac_address_reason_code
                                   : UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
                           profile ? profile->invalid_device_mac_address_surface
                                   : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }
    if (validate_device_mac_address(profile, device_mac_value, device_mac_value_len, mac_address_mode, result) != 0) {
        return -1;
    }
    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_DST_MAC_ADDRESS, dst_mac_value, &dst_mac_value_len) != 0) {
        set_invalid_result(result,
                           profile ? profile->invalid_dst_mac_address_status : UCI_STATUS_INVALID_PARAM,
                           profile ? profile->invalid_dst_mac_address_reason_code
                                   : UCI_SESSION_REASON_ERROR_INVALID_DST_ADDRESS_LIST,
                           profile ? profile->invalid_dst_mac_address_surface
                                   : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }
    if (validate_dst_mac_address(profile, dst_mac_value, dst_mac_value_len, result) != 0) {
        return -1;
    }

    if ((dst_mac_value_len / 2U) != number_of_controlees) {
        set_invalid_result(result,
                           profile ? profile->invalid_num_of_controlees_status : UCI_STATUS_INVALID_PARAM,
                           profile ? profile->invalid_num_of_controlees_reason_code
                                   : UCI_SESSION_REASON_ERROR_INVALID_NUM_OF_CONTROLEES,
                           profile ? profile->invalid_num_of_controlees_surface
                                   : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }

    if (multi_node_mode == 0x00U && (number_of_controlees != 1U || dst_mac_value_len != 2U)) {
        set_invalid_result(result, status, reason, surface);
        return -1;
    }

    return 0;
}

static int require_session_config_len(const uci_sim_profile_t* profile,
                                      const uci_sim_session_t* session,
                                      uint8_t config_id,
                                      uint8_t expected_len,
                                      uci_sim_validation_result_t* result) {
    uint8_t value_len = 0;

    if (!session) {
        return 0;
    }

    if (uci_sim_session_get_config(session, config_id, NULL, &value_len) != 0 ||
        value_len != expected_len) {
        set_invalid_result(result,
                           profile ? profile->invalid_sts_config_status : UCI_STATUS_INVALID_PARAM,
                           profile ? profile->invalid_sts_config_reason_code
                                   : UCI_SESSION_REASON_ERROR_INVALID_STS_CONFIG,
                           profile ? profile->invalid_sts_config_surface
                                   : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }

    return 0;
}

static int require_session_config_len_one_of(const uci_sim_profile_t* profile,
                                             const uci_sim_session_t* session,
                                             uint8_t config_id,
                                             uint8_t expected_len_a,
                                             uint8_t expected_len_b,
                                             uci_sim_validation_result_t* result) {
    uint8_t value_len = 0;

    if (!session) {
        return 0;
    }

    if (uci_sim_session_get_config(session, config_id, NULL, &value_len) != 0 ||
        (value_len != expected_len_a && value_len != expected_len_b)) {
        set_invalid_result(result,
                           profile ? profile->invalid_sts_config_status : UCI_STATUS_INVALID_PARAM,
                           profile ? profile->invalid_sts_config_reason_code
                                   : UCI_SESSION_REASON_ERROR_INVALID_STS_CONFIG,
                           profile ? profile->invalid_sts_config_surface
                                   : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }

    return 0;
}

static int validate_sts_dependencies(const uci_sim_profile_t* profile,
                                     const uci_sim_session_t* session,
                                     uint8_t sts_config,
                                     uci_sim_validation_result_t* result) {
    switch (sts_config) {
        case 0x00:
            return require_session_config_len(profile, session, UCI_APP_CONFIG_STATIC_STS_IV, 8, result);
        case 0x03:
            return require_session_config_len_one_of(profile, session, UCI_APP_CONFIG_SESSION_KEY, 16, 32, result);
        case 0x04:
            if (require_session_config_len_one_of(profile, session, UCI_APP_CONFIG_SESSION_KEY, 16, 32, result) != 0) {
                return -1;
            }
            return require_session_config_len_one_of(profile, session, UCI_APP_CONFIG_SUBSESSION_KEY, 16, 32, result);
        default:
            return 0;
    }
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
        if (config_id == UCI_APP_CONFIG_DEVICE_TYPE) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_device_type_reason_code
                                           : UCI_SESSION_REASON_STATE_CHANGE_WITH_SESSION_MANAGEMENT_COMMANDS,
                                   profile ? profile->invalid_device_type_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_device_type(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_MULTI_NODE_MODE) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_multi_node_mode_reason_code
                                           : UCI_SESSION_REASON_ERROR_INVALID_MULTI_NODE_MODE,
                                   profile ? profile->invalid_multi_node_mode_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_multi_node_mode(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_NUMBER_OF_CONTROLEES) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_num_of_controlees_reason_code
                                           : UCI_SESSION_REASON_ERROR_INVALID_NUM_OF_CONTROLEES,
                                   profile ? profile->invalid_num_of_controlees_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_number_of_controlees(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_MAC_ADDRESS_MODE) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_mac_address_mode_reason_code
                                           : UCI_SESSION_REASON_ERROR_MAC_ADDRESS_MODE_NOT_SUPPORTED,
                                   profile ? profile->invalid_mac_address_mode_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_mac_address_mode(profile, value[0], result);
        }
        if (config_id == UCI_APP_CONFIG_DEVICE_MAC_ADDRESS) {
            uint8_t mac_address_mode = UCI_MAC_ADDRESS_MODE_SHORT;
            uint8_t mode_len = 0U;

            if (session &&
                uci_sim_session_get_config(session, UCI_APP_CONFIG_MAC_ADDRESS_MODE,
                                           &mac_address_mode, &mode_len) == 0 &&
                mode_len == 1U) {
                return validate_device_mac_address(profile, value, value_len, mac_address_mode, result);
            }
            return validate_device_mac_address(profile, value, value_len, UCI_MAC_ADDRESS_MODE_SHORT, result);
        }
        if (config_id == UCI_APP_CONFIG_DST_MAC_ADDRESS) {
            return validate_dst_mac_address(profile, value, value_len, result);
        }
        if (config_id == UCI_APP_CONFIG_STS_CONFIG) {
            if (!value || value_len != 1) {
                set_invalid_result(result,
                                   UCI_STATUS_INVALID_PARAM,
                                   profile ? profile->invalid_sts_config_reason_code
                                           : UCI_SESSION_REASON_ERROR_INVALID_STS_CONFIG,
                                   profile ? profile->invalid_sts_config_surface
                                           : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
                return -1;
            }
            return validate_sts_config(profile, value[0], result);
        }
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
    uint8_t sts_config = 0x00;
    uint8_t sts_config_len = 0;
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

    if (validate_device_type_role_pair(profile, session, result) != 0) {
        return -1;
    }
    if (validate_multi_node_mode_topology(profile, session, result) != 0) {
        return -1;
    }

    if (uci_sim_session_get_config(session, UCI_APP_CONFIG_STS_CONFIG, &sts_config, &sts_config_len) != 0 ||
        sts_config_len != 1) {
        set_invalid_result(result,
                           profile ? profile->invalid_sts_config_status : UCI_STATUS_INVALID_PARAM,
                           profile ? profile->invalid_sts_config_reason_code
                                   : UCI_SESSION_REASON_ERROR_INVALID_STS_CONFIG,
                           profile ? profile->invalid_sts_config_surface
                                   : UCI_SIM_INVALID_CONFIG_SURFACE_IMMEDIATE);
        return -1;
    }

    if (validate_sts_config(profile, sts_config, result) != 0) {
        return -1;
    }
    if (validate_sts_dependencies(profile, session, sts_config, result) != 0) {
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
