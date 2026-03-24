#include "uci_sim_scenario.h"
#include "uci_sim_device.h"

#include <string.h>

static uint32_t time_base_offset_delay_ms(uint32_t offset_us) {
    return (offset_us + 999U) / 1000U;
}

static int find_pending_range_delay(const uci_sim_device_t* device,
                                    uint32_t session_id,
                                    uint32_t* delay_ms) {
    size_t i;

    if (!device || !delay_ms) {
        return -1;
    }

    for (i = 0; i < device->scheduled_event_count; ++i) {
        if (device->scheduled_events[i].type == UCI_SIM_EVENT_RANGE_DATA &&
            device->scheduled_events[i].session_id == session_id) {
            *delay_ms = device->scheduled_events[i].delay_ms;
            return 0;
        }
    }

    return -1;
}

static uint32_t calculate_session_start_delay_ms(const uci_sim_device_t* device,
                                                 const uci_sim_session_t* session,
                                                 const uci_sim_profile_t* profile) {
    uci_sim_session_time_base_t time_base;
    const uci_sim_session_t* reference_session = NULL;
    uint32_t reference_delay_ms = 0U;
    uint32_t default_delay_ms;
    size_t i;

    default_delay_ms = uci_sim_session_get_ranging_interval_ms(session, profile);
    if (!device || !session || uci_sim_session_get_session_time_base(session, &time_base) != 0 ||
        !time_base.present || !time_base.enabled) {
        return default_delay_ms;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        if (device->sessions[i].allocated &&
            device->sessions[i].session_id == time_base.reference_session_id) {
            reference_session = &device->sessions[i];
            break;
        }
    }

    if (!reference_session || reference_session->state != UCI_SESSION_STATE_ACTIVE) {
        return default_delay_ms;
    }

    if (find_pending_range_delay(device, time_base.reference_session_id, &reference_delay_ms) != 0) {
        return default_delay_ms;
    }

    return reference_delay_ms + time_base_offset_delay_ms(time_base.offset_us);
}

static void resync_time_base_dependents(uci_sim_device_t* device,
                                        const uci_sim_session_t* reference_session,
                                        const uci_sim_profile_t* profile) {
    size_t i;
    uint32_t reference_delay_ms = 0U;

    if (!device || !reference_session) {
        return;
    }

    if (find_pending_range_delay(device, reference_session->session_id, &reference_delay_ms) != 0) {
        return;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        uci_sim_session_t* dependent_session = &device->sessions[i];
        uci_sim_session_time_base_t time_base;

        if (!dependent_session->allocated ||
            dependent_session->session_id == reference_session->session_id ||
            dependent_session->state != UCI_SESSION_STATE_ACTIVE) {
            continue;
        }

        if (uci_sim_session_get_session_time_base(dependent_session, &time_base) != 0 ||
            !time_base.present || !time_base.enabled || !time_base.resync ||
            time_base.reference_session_id != reference_session->session_id) {
            continue;
        }

        (void)profile;
        (void)uci_sim_device_reschedule_session_event(device,
                                                      UCI_SIM_EVENT_RANGE_DATA,
                                                      dependent_session->session_id,
                                                      reference_delay_ms + time_base_offset_delay_ms(time_base.offset_us));
    }
}

static void handle_reference_session_stopped(uci_sim_device_t* device,
                                             const uci_sim_session_t* reference_session) {
    size_t i;

    if (!device || !reference_session) {
        return;
    }

    for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
        uci_sim_session_t* dependent_session = &device->sessions[i];
        uci_sim_session_time_base_t time_base;

        if (!dependent_session->allocated ||
            dependent_session->session_id == reference_session->session_id ||
            dependent_session->state != UCI_SESSION_STATE_ACTIVE) {
            continue;
        }

        if (uci_sim_session_get_session_time_base(dependent_session, &time_base) != 0 ||
            !time_base.present || !time_base.enabled ||
            time_base.reference_session_id != reference_session->session_id ||
            time_base.continue_session) {
            continue;
        }

        dependent_session->state = UCI_SESSION_STATE_IDLE;
        dependent_session->ranging_stream_remaining = 0U;
        uci_sim_device_cancel_session_events(device, dependent_session->session_id);
        (void)uci_sim_device_queue_session_status_notification(
            device,
            dependent_session->session_id,
            UCI_SESSION_STATE_IDLE,
            UCI_SESSION_REASON_ERROR_REF_UWB_SESSION_LOST);
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
    const uci_sim_profile_t* profile;
    uint32_t delay_ms;

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
    delay_ms = calculate_session_start_delay_ms(device, session, profile);
    if (uci_sim_device_schedule_event(device,
                                      UCI_SIM_EVENT_RANGE_DATA,
                                      session->session_id,
                                      delay_ms) != 0) {
        return -1;
    }
    resync_time_base_dependents(device, session, profile);
    return 0;
}

void uci_sim_scenario_on_session_stopped(uci_sim_device_t* device, uci_sim_session_t* session) {
    if (!device || !session) {
        return;
    }

    session->ranging_stream_remaining = 0;
    session->data_transfer_in_progress = 0;
    session->data_transfer_repetitions_remaining = 0;
    session->data_transfer_tx_count = 0;
    uci_sim_device_cancel_session_events(device, session->session_id);
    handle_reference_session_stopped(device, session);
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
