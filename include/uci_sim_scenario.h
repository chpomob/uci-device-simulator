#ifndef UCI_SIM_SCENARIO_H
#define UCI_SIM_SCENARIO_H

#include <stddef.h>
#include <stdint.h>

#include "uci_sim_packet.h"

typedef struct uci_sim_device uci_sim_device_t;
typedef struct uci_sim_session uci_sim_session_t;
typedef struct uci_sim_result uci_sim_result_t;

typedef enum {
    UCI_SIM_SCENARIO_DEFAULT = 0,
    UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS = 1,
    UCI_SIM_SCENARIO_RANGING_STREAM = 2,
} uci_sim_scenario_kind_t;

const char* uci_sim_scenario_name(uci_sim_scenario_kind_t scenario);
int uci_sim_scenario_parse(const char* name, uci_sim_scenario_kind_t* scenario);
int uci_sim_scenario_should_defer_notification(uci_sim_scenario_kind_t scenario);
int uci_sim_scenario_should_auto_deliver_pending(uci_sim_scenario_kind_t scenario,
                                                 size_t pending_count_before);
int uci_sim_scenario_on_session_started(uci_sim_device_t* device,
                                        uci_sim_session_t* session,
                                        uci_sim_result_t* result);
void uci_sim_scenario_on_session_stopped(uci_sim_device_t* device, uci_sim_session_t* session);
int uci_sim_scenario_on_command_complete(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result);

#endif
