#include "uci_sim_scenario.h"
#include "uci_sim_device.h"

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

int uci_sim_scenario_should_defer_notification(uci_sim_scenario_kind_t scenario) {
    return scenario == UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS;
}

int uci_sim_scenario_should_auto_deliver_pending(uci_sim_scenario_kind_t scenario,
                                                 size_t pending_count_before) {
    if (scenario == UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS && pending_count_before == 0) {
        return 0;
    }
    return 1;
}

int uci_sim_scenario_on_session_started(uci_sim_device_t* device,
                                        uci_sim_session_t* session,
                                        uci_sim_result_t* result) {
    if (!device || !session || !result) {
        return -1;
    }

    if (device->scenario != UCI_SIM_SCENARIO_RANGING_STREAM) {
        return 0;
    }

    session->ranging_stream_remaining = 3;
    return uci_sim_device_emit_ranging_stream(device, session, result);
}

void uci_sim_scenario_on_session_stopped(uci_sim_session_t* session) {
    if (!session) {
        return;
    }

    session->ranging_stream_remaining = 0;
}

int uci_sim_scenario_on_command_complete(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result) {
    size_t i;

    if (!device || !request || !result || device->scenario != UCI_SIM_SCENARIO_RANGING_STREAM) {
        return 0;
    }
    if (request->gid == UCI_GID_SESSION_CONTROL &&
        (request->oid == UCI_SESSION_START || request->oid == UCI_SESSION_STOP)) {
        return 0;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (device->sessions[i].allocated &&
            device->sessions[i].state == UCI_SESSION_STATE_ACTIVE &&
            device->sessions[i].ranging_stream_remaining > 0) {
            return uci_sim_device_emit_ranging_stream(device, &device->sessions[i], result);
        }
    }

    return 0;
}
