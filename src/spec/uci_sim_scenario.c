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
    const uci_sim_profile_t* profile;

    if (!device || !session || !result) {
        return -1;
    }

    if (device->scenario != UCI_SIM_SCENARIO_RANGING_STREAM) {
        return 0;
    }

    profile = device->profile ? device->profile : uci_sim_default_profile();
    (void)result;
    session->ranging_stream_remaining = profile->ranging_stream_burst_count;
    if (session->ranging_stream_remaining == 0) {
        return 0;
    }
    return uci_sim_device_schedule_event(device,
                                         UCI_SIM_EVENT_RANGE_DATA,
                                         session->session_id,
                                         uci_sim_session_get_ranging_interval_ms(session, profile));
}

void uci_sim_scenario_on_session_stopped(uci_sim_device_t* device, uci_sim_session_t* session) {
    if (!device || !session) {
        return;
    }

    session->ranging_stream_remaining = 0;
    uci_sim_device_cancel_session_events(device, session->session_id);
}

int uci_sim_scenario_on_command_complete(uci_sim_device_t* device,
                                         const uci_sim_packet_t* request,
                                         uci_sim_result_t* result) {
    if (!device || !request || !result) {
        return 0;
    }
    (void)request;
    (void)result;
    return 0;
}
