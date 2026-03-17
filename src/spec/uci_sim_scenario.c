#include "uci_sim_scenario.h"

#include <string.h>

const char* uci_sim_scenario_name(uci_sim_scenario_kind_t scenario) {
    switch (scenario) {
        case UCI_SIM_SCENARIO_DEFAULT:
            return "default";
        case UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS:
            return "delayed_notifications";
        case UCI_SIM_SCENARIO_RANGING_STREAM:
            return "ranging_stream";
        default:
            return "unknown";
    }
}

int uci_sim_scenario_parse(const char* name, uci_sim_scenario_kind_t* scenario) {
    if (!name || !scenario) {
        return -1;
    }

    if (strcmp(name, "default") == 0) {
        *scenario = UCI_SIM_SCENARIO_DEFAULT;
        return 0;
    }
    if (strcmp(name, "delayed_notifications") == 0) {
        *scenario = UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS;
        return 0;
    }
    if (strcmp(name, "ranging_stream") == 0) {
        *scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
        return 0;
    }

    return -1;
}
