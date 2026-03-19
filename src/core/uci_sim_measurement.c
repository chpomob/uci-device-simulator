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
    uint8_t result_report_config;
    uint16_t proximity_near_cm;
    uint16_t proximity_far_cm;
    uint8_t in_proximity_range;

    if (!session || !sample || !result) {
        return;
    }

    memset(result, 0, sizeof(*result));

    ntf_config = uci_sim_session_get_range_data_ntf_config(session);
    result_report_config = uci_sim_session_get_result_report_config(session);
    proximity_near_cm = uci_sim_session_get_range_data_ntf_proximity_near(session);
    proximity_far_cm = uci_sim_session_get_range_data_ntf_proximity_far(session);
    in_proximity_range = (proximity_near_cm <= proximity_far_cm &&
                          sample->distance_cm >= proximity_near_cm &&
                          sample->distance_cm <= proximity_far_cm) ? 1U : 0U;

    result->has_proximity_state = 1U;
    result->in_proximity_range = in_proximity_range;
    result->emitted_field_mask = UCI_SIM_MEAS_FIELD_RSSI;
    if ((result_report_config & 0x01U) != 0U) {
        result->emitted_field_mask |= UCI_SIM_MEAS_FIELD_DISTANCE;
    }
    if ((result_report_config & 0x02U) != 0U) {
        result->emitted_field_mask |= UCI_SIM_MEAS_FIELD_AOA_AZIMUTH;
    }
    if ((result_report_config & 0x04U) != 0U) {
        result->emitted_field_mask |= UCI_SIM_MEAS_FIELD_AOA_ELEVATION;
    }
    if ((result_report_config & 0x08U) != 0U) {
        result->emitted_field_mask |= UCI_SIM_MEAS_FIELD_AOA_FOM;
    }

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
                                                      const uci_sim_measurement_policy_result_t* policy_result,
                                                      uint8_t notification_oid,
                                                      uci_sim_packet_t* notification) {
    uint8_t* payload;
    uint8_t distance_offset;
    uint8_t local_azimuth_offset;
    uint8_t local_azimuth_fom_offset;
    uint8_t local_elevation_offset;
    uint8_t local_elevation_fom_offset;
    uint8_t remote_azimuth_offset;
    uint8_t remote_azimuth_fom_offset;
    uint8_t remote_elevation_offset;
    uint8_t remote_elevation_fom_offset;

    if (!profile || !sample || !policy_result || !notification) {
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

    distance_offset = profile->range_data_measurement_distance_offset;
    local_azimuth_offset = (uint8_t)(distance_offset + 2U);
    local_azimuth_fom_offset = (uint8_t)(local_azimuth_offset + 2U);
    local_elevation_offset = (uint8_t)(local_azimuth_fom_offset + 1U);
    local_elevation_fom_offset = (uint8_t)(local_elevation_offset + 2U);
    remote_azimuth_offset = (uint8_t)(local_elevation_fom_offset + 1U);
    remote_azimuth_fom_offset = (uint8_t)(remote_azimuth_offset + 2U);
    remote_elevation_offset = (uint8_t)(remote_azimuth_fom_offset + 1U);
    remote_elevation_fom_offset = (uint8_t)(remote_elevation_offset + 2U);

    if ((policy_result->emitted_field_mask & UCI_SIM_MEAS_FIELD_DISTANCE) != 0U) {
        payload[distance_offset] = (uint8_t)(sample->distance_cm & 0xFFU);
        payload[distance_offset + 1U] = (uint8_t)((sample->distance_cm >> 8) & 0xFFU);
    } else {
        payload[distance_offset] = 0x00;
        payload[distance_offset + 1U] = 0x00;
    }

    if ((policy_result->emitted_field_mask & UCI_SIM_MEAS_FIELD_AOA_AZIMUTH) == 0U) {
        payload[local_azimuth_offset] = 0x00;
        payload[local_azimuth_offset + 1U] = 0x00;
        payload[remote_azimuth_offset] = 0x00;
        payload[remote_azimuth_offset + 1U] = 0x00;
        payload[local_azimuth_fom_offset] = 0x00;
        payload[remote_azimuth_fom_offset] = 0x00;
    }

    if ((policy_result->emitted_field_mask & UCI_SIM_MEAS_FIELD_AOA_ELEVATION) == 0U) {
        payload[local_elevation_offset] = 0x00;
        payload[local_elevation_offset + 1U] = 0x00;
        payload[remote_elevation_offset] = 0x00;
        payload[remote_elevation_offset + 1U] = 0x00;
        payload[local_elevation_fom_offset] = 0x00;
        payload[remote_elevation_fom_offset] = 0x00;
    }

    if ((policy_result->emitted_field_mask & UCI_SIM_MEAS_FIELD_AOA_FOM) == 0U) {
        payload[local_azimuth_fom_offset] = 0x00;
        payload[local_elevation_fom_offset] = 0x00;
        payload[remote_azimuth_fom_offset] = 0x00;
        payload[remote_elevation_fom_offset] = 0x00;
    }

    return 0;
}
