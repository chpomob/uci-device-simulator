#ifndef UCI_SIM_VALIDATION_H
#define UCI_SIM_VALIDATION_H

#include "uci_sim_device.h"

typedef struct {
    int ok;
    uint8_t status;
    uint8_t reason;
    uint8_t surface;
} uci_sim_validation_result_t;

void uci_sim_validation_result_init(uci_sim_validation_result_t* result);
int uci_sim_validate_session_app_config(const uci_sim_profile_t* profile,
                                        const uci_sim_session_t* session,
                                        uint8_t config_id,
                                        const uint8_t* value,
                                        uint8_t value_len,
                                        uci_sim_validation_result_t* result);
int uci_sim_validate_session_start(const uci_sim_profile_t* profile,
                                   const uci_sim_session_t* session,
                                   uci_sim_validation_result_t* result);

#endif
