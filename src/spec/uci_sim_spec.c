#include "uci_sim_spec.h"

const char* uci_sim_status_name(uint8_t status) {
    switch (status) {
        case UCI_STATUS_OK: return "OK";
        case UCI_STATUS_REJECTED: return "REJECTED";
        case UCI_STATUS_FAILED: return "FAILED";
        case UCI_STATUS_SYNTAX_ERROR: return "SYNTAX_ERROR";
        case UCI_STATUS_INVALID_PARAM: return "INVALID_PARAM";
        case UCI_STATUS_INVALID_RANGE: return "INVALID_RANGE";
        case UCI_STATUS_INVALID_MSG_SIZE: return "INVALID_MSG_SIZE";
        case UCI_STATUS_UNKNOWN_GID: return "UNKNOWN_GID";
        case UCI_STATUS_UNKNOWN_OID: return "UNKNOWN_OID";
        default: return "UNKNOWN_STATUS";
    }
}

const char* uci_sim_gid_name(uint8_t gid) {
    switch (gid) {
        case UCI_GID_CORE: return "CORE";
        case UCI_GID_SESSION_CONFIG: return "SESSION_CONFIG";
        case UCI_GID_SESSION_CONTROL: return "SESSION_CONTROL";
        default: return "UNKNOWN_GID";
    }
}
