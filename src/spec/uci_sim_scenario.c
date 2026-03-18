#include "uci_sim_scenario.h"
#include "uci_sim_device.h"

#include <string.h>

static uci_sim_session_t* find_session_by_id(uci_sim_device_t* device, uint32_t session_id) {
    size_t i;

    if (!device) {
        return NULL;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (device->sessions[i].allocated && device->sessions[i].session_id == session_id) {
            return &device->sessions[i];
        }
    }

    return NULL;
}

static int process_ready_event(uci_sim_device_t* device,
                               const uci_sim_scheduled_event_t* event,
                               uci_sim_result_t* result) {
    uci_sim_session_t* session;

    if (!device || !event || !result) {
        return -1;
    }

    switch (event->type) {
        case UCI_SIM_EVENT_RANGE_DATA:
            session = find_session_by_id(device, event->session_id);
            if (!session) {
                return 0;
            }
            if (uci_sim_device_emit_ranging_stream(device, session, result) != 0) {
                return -1;
            }
            if (session->ranging_stream_remaining > 0 && session->state == UCI_SESSION_STATE_ACTIVE) {
                return uci_sim_device_schedule_event(device,
                                                     UCI_SIM_EVENT_RANGE_DATA,
                                                     session->session_id,
                                                     1);
            }
            return 0;
        case UCI_SIM_EVENT_NONE:
        default:
            return 0;
    }
}

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
    return uci_sim_device_schedule_event(device, UCI_SIM_EVENT_RANGE_DATA, session->session_id, 0);
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
    uci_sim_scheduled_event_t event;

    (void)request;
    if (!device || !request || !result) {
        return 0;
    }
    if (device->scenario != UCI_SIM_SCENARIO_RANGING_STREAM) {
        return 0;
    }

    uci_sim_device_tick_events(device);
    if (uci_sim_device_dequeue_ready_event(device, &event) == 0) {
        return process_ready_event(device, &event, result);
    }

    return 0;
}
