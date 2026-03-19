#include "uci_sim_measurement.h"

#include <string.h>

static void write_u32_le(uint8_t* payload, uint32_t value) {
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)((value >> 8) & 0xFFU);
    payload[2] = (uint8_t)((value >> 16) & 0xFFU);
    payload[3] = (uint8_t)((value >> 24) & 0xFFU);
}

void uci_sim_measurement_init_ranging_sample(const uci_sim_profile_t* profile,
                                             const uci_sim_session_t* session,
                                             uci_sim_measurement_t* sample) {
    uint32_t distance_cm;

    if (!profile || !session || !sample) {
        return;
    }

    memset(sample, 0, sizeof(*sample));
    distance_cm = profile->range_data_distance_base_cm +
                  (session->ranging_count * profile->range_data_distance_step_cm);

    sample->session_id = session->session_id;
    sample->ranging_interval_ms = profile->ranging_interval_ms;
    sample->distance_cm = (uint16_t)distance_cm;
}

void uci_sim_measurement_evaluate_range_notification_policy(
    const uci_sim_session_t* session,
    const uci_sim_measurement_t* sample,
    uci_sim_measurement_policy_result_t* result) {
    uint8_t ntf_config;
    uint16_t proximity_near_cm;
    uint16_t proximity_far_cm;
    uint8_t in_proximity_range;

    if (!session || !sample || !result) {
        return;
    }

    memset(result, 0, sizeof(*result));

    ntf_config = uci_sim_session_get_range_data_ntf_config(session);
    proximity_near_cm = uci_sim_session_get_range_data_ntf_proximity_near(session);
    proximity_far_cm = uci_sim_session_get_range_data_ntf_proximity_far(session);
    in_proximity_range = (proximity_near_cm <= proximity_far_cm &&
                          sample->distance_cm >= proximity_near_cm &&
                          sample->distance_cm <= proximity_far_cm) ? 1U : 0U;

    result->has_proximity_state = 1U;
    result->in_proximity_range = in_proximity_range;

    switch (ntf_config) {
        case 0x00:
            result->should_emit_notification = 0;
            break;
        case 0x02:
            result->should_emit_notification = in_proximity_range;
            break;
        case 0x05:
            result->should_emit_notification =
                session->has_last_proximity_state &&
                (session->last_in_proximity_range != in_proximity_range);
            break;
        default:
            result->should_emit_notification = 1;
            break;
    }
}

int uci_sim_measurement_build_range_data_notification(const uci_sim_profile_t* profile,
                                                      const uci_sim_measurement_t* sample,
                                                      uint8_t notification_oid,
                                                      uci_sim_packet_t* notification) {
    uint8_t* payload;

    if (!profile || !sample || !notification) {
        return -1;
    }
    if (profile->range_data_payload_len == 0 ||
        profile->range_data_payload_len > UCI_SIM_MAX_PAYLOAD) {
        return -1;
    }

    memset(notification, 0, sizeof(*notification));
    notification->mt = UCI_MT_NOTIFICATION;
    notification->pbf = UCI_PBF_COMPLETE;
    notification->gid = UCI_GID_SESSION_CONTROL;
    notification->oid = notification_oid;
    notification->payload_len = profile->range_data_payload_len;

    payload = notification->payload;
    memcpy(payload, profile->range_data_payload_template, profile->range_data_payload_len);

    write_u32_le(&payload[profile->range_data_sequence_offset], sample->sequence_number);
    write_u32_le(&payload[profile->range_data_primary_session_id_offset], sample->session_id);
    write_u32_le(&payload[profile->range_data_secondary_session_id_offset], sample->session_id);
    write_u32_le(&payload[profile->range_data_interval_offset], sample->ranging_interval_ms);
    payload[profile->range_data_measurement_distance_offset] =
        (uint8_t)(sample->distance_cm & 0xFFU);
    payload[profile->range_data_measurement_distance_offset + 1] =
        (uint8_t)((sample->distance_cm >> 8) & 0xFFU);

    return 0;
}
