#include "uci_sim_engine.h"

#include <string.h>

#define UCI_SIM_RANGING_EVENT_PERIOD_MS 1000U

static int queue_outbound_packet(uci_sim_engine_t* engine, const uci_sim_packet_t* packet) {
    if (!engine || !packet) {
        return -1;
    }
    if (engine->outbound_count >= UCI_SIM_MAX_OUTBOUND_PACKETS) {
        return -1;
    }

    engine->outbound_packets[engine->outbound_count++] = *packet;
    return 0;
}

static void flush_device_notifications(uci_sim_engine_t* engine) {
    uci_sim_packet_t packet;

    if (!engine || uci_sim_scenario_should_defer_notification(engine->device.scenario)) {
        return;
    }

    while (uci_sim_device_dequeue_notification(&engine->device, &packet) == 0) {
        if (queue_outbound_packet(engine, &packet) != 0) {
            break;
        }
    }
}

static int process_ready_event(uci_sim_engine_t* engine,
                               const uci_sim_scheduled_event_t* event) {
    uci_sim_session_t* session = NULL;
    size_t i;
    uci_sim_result_t result;

    if (!engine || !event) {
        return -1;
    }

    memset(&result, 0, sizeof(result));

    switch (event->type) {
        case UCI_SIM_EVENT_RANGE_DATA:
            for (i = 0; i < UCI_SIM_MAX_SESSIONS; ++i) {
                if (engine->device.sessions[i].allocated &&
                    engine->device.sessions[i].session_id == event->session_id) {
                    session = &engine->device.sessions[i];
                    break;
                }
            }
            if (!session) {
                return 0;
            }
            if (uci_sim_device_emit_ranging_stream(&engine->device, session, &result) != 0) {
                return -1;
            }
            flush_device_notifications(engine);
            if (session->ranging_stream_remaining > 0 && session->state == UCI_SESSION_STATE_ACTIVE) {
                uint32_t delay_ms = (session->ranging_count < 2) ? 0U : UCI_SIM_RANGING_EVENT_PERIOD_MS;
                return uci_sim_device_schedule_event(&engine->device,
                                                     UCI_SIM_EVENT_RANGE_DATA,
                                                     session->session_id,
                                                     delay_ms);
            }
            return 0;
        case UCI_SIM_EVENT_NONE:
        default:
            return 0;
    }
}

void uci_sim_engine_init(uci_sim_engine_t* engine) {
    uci_sim_engine_init_with_scenario(engine, UCI_SIM_SCENARIO_DEFAULT);
}

void uci_sim_engine_init_with_scenario(uci_sim_engine_t* engine, uci_sim_scenario_kind_t scenario) {
    if (!engine) {
        return;
    }

    memset(engine, 0, sizeof(*engine));
    uci_sim_device_init_with_scenario(&engine->device, scenario);
}

int uci_sim_engine_submit_packet(uci_sim_engine_t* engine, const uci_sim_packet_t* request) {
    uci_sim_result_t result;

    if (!engine || !request) {
        return -1;
    }

    if (uci_sim_device_handle_packet(&engine->device, request, &result) != 0 && !result.has_response) {
        return -1;
    }

    if (result.has_response && queue_outbound_packet(engine, &result.response) != 0) {
        return -1;
    }
    if (result.has_notification && queue_outbound_packet(engine, &result.notification) != 0) {
        return -1;
    }

    flush_device_notifications(engine);
    return uci_sim_engine_tick(engine, 0);
}

int uci_sim_engine_tick(uci_sim_engine_t* engine, uint32_t elapsed_ms) {
    uci_sim_scheduled_event_t event;

    if (!engine) {
        return -1;
    }

    uci_sim_device_tick_events(&engine->device, elapsed_ms);
    if (uci_sim_device_dequeue_ready_event(&engine->device, &event) == 0) {
        if (process_ready_event(engine, &event) != 0) {
            return -1;
        }
    }

    return 0;
}

int uci_sim_engine_dequeue_outbound_packet(uci_sim_engine_t* engine, uci_sim_packet_t* packet) {
    size_t i;

    if (!engine || !packet || engine->outbound_count == 0) {
        return -1;
    }

    *packet = engine->outbound_packets[0];
    for (i = 1; i < engine->outbound_count; ++i) {
        engine->outbound_packets[i - 1] = engine->outbound_packets[i];
    }
    engine->outbound_count--;
    return 0;
}
