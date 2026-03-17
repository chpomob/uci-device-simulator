#ifndef UCI_SIM_SCENARIO_H
#define UCI_SIM_SCENARIO_H

#include <stdint.h>

typedef enum {
    UCI_SIM_SCENARIO_DEFAULT = 0,
    UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS = 1,
    UCI_SIM_SCENARIO_RANGING_STREAM = 2,
} uci_sim_scenario_kind_t;

const char* uci_sim_scenario_name(uci_sim_scenario_kind_t scenario);
int uci_sim_scenario_parse(const char* name, uci_sim_scenario_kind_t* scenario);

#endif
