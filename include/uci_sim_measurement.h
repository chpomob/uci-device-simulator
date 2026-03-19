#ifndef UCI_SIM_MEASUREMENT_H
#define UCI_SIM_MEASUREMENT_H

#include "uci_sim_device.h"

typedef struct {
    uint32_t session_id;
    uint32_t sequence_number;
    uint32_t ranging_interval_ms;
    uint16_t distance_cm;
    uint8_t measurement_type;
    uint8_t nlos;
    int16_t local_aoa_azimuth_q97;
    int16_t local_aoa_elevation_q97;
    int16_t remote_aoa_azimuth_q97;
    int16_t remote_aoa_elevation_q97;
    uint8_t local_aoa_azimuth_fom;
    uint8_t local_aoa_elevation_fom;
    uint8_t remote_aoa_azimuth_fom;
    uint8_t remote_aoa_elevation_fom;
    uint8_t slot_index;
    uint8_t rssi;
} uci_sim_measurement_t;

typedef struct {
    int should_emit_notification;
    uint8_t has_proximity_state;
    uint8_t in_proximity_range;
    uint8_t emitted_field_mask;
} uci_sim_measurement_policy_result_t;

enum {
    UCI_SIM_MEAS_FIELD_DISTANCE = 0x01,
    UCI_SIM_MEAS_FIELD_AOA_AZIMUTH = 0x02,
    UCI_SIM_MEAS_FIELD_AOA_ELEVATION = 0x04,
    UCI_SIM_MEAS_FIELD_AOA_AZIMUTH_FOM = 0x08,
    UCI_SIM_MEAS_FIELD_AOA_ELEVATION_FOM = 0x10,
    UCI_SIM_MEAS_FIELD_RSSI = 0x20
};

void uci_sim_measurement_init_ranging_sample(const uci_sim_profile_t* profile,
                                             const uci_sim_session_t* session,
                                             uci_sim_measurement_t* sample);
void uci_sim_measurement_evaluate_range_notification_policy(
    const uci_sim_session_t* session,
    const uci_sim_measurement_t* sample,
    uci_sim_measurement_policy_result_t* result);
int uci_sim_measurement_build_range_data_notification(const uci_sim_profile_t* profile,
                                                      const uci_sim_measurement_t* sample,
                                                      const uci_sim_measurement_policy_result_t* policy_result,
                                                      uint8_t notification_oid,
                                                      uci_sim_packet_t* notification);

#endif
