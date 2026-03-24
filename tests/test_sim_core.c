#include "uci_sim_device.h"
#include "uci_sim_engine.h"
#include "uci_sim_measurement.h"
#include "uci_sim_profile.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;
static int g_passed = 0;
static uci_sim_time_ms_t g_fake_clock_ms = 0;

#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_U8(exp, act, msg) do { if ((unsigned)(exp) != (unsigned)(act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_U16(exp, act, msg) do { if ((unsigned)(exp) != (unsigned)(act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_U32(exp, act, msg) do { if ((unsigned long)(exp) != (unsigned long)(act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define PASS() do { g_passed++; } while (0)

static uint32_t read_u32_le(const uint8_t* payload) {
    return (uint32_t)payload[0] |
           ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) |
           ((uint32_t)payload[3] << 24);
}

static uint16_t read_u16_le(const uint8_t* payload) {
    return (uint16_t)payload[0] |
           (uint16_t)((uint16_t)payload[1] << 8);
}

static uint64_t read_u64_le(const uint8_t* payload) {
    return (uint64_t)payload[0] |
           ((uint64_t)payload[1] << 8) |
           ((uint64_t)payload[2] << 16) |
           ((uint64_t)payload[3] << 24) |
           ((uint64_t)payload[4] << 32) |
           ((uint64_t)payload[5] << 40) |
           ((uint64_t)payload[6] << 48) |
           ((uint64_t)payload[7] << 56);
}

static void test_measurement_policy_serializes_default_range_notification(void) {
    const uci_sim_profile_t* profile = uci_sim_default_profile();
    uci_sim_session_t session;
    uci_sim_measurement_t measurement;
    uci_sim_measurement_policy_result_t policy_result;
    uci_sim_packet_t notification;

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    session.ranging_count = 2;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             (const uint8_t[]){0x0F},
                                             1) == 0,
                "measurement default result report config store failed");

    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 7U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);

    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "measurement notification build failed");
    ASSERT_EQ_U8(UCI_MT_NOTIFICATION, notification.mt, "measurement notification mt");
    ASSERT_EQ_U8(UCI_GID_SESSION_CONTROL, notification.gid, "measurement notification gid");
    ASSERT_EQ_U8(profile->range_data_notification_oid, notification.oid, "measurement notification oid");
    ASSERT_EQ_U32(7U, read_u32_le(&notification.payload[profile->range_data_sequence_offset]),
                  "measurement notification sequence");
    ASSERT_EQ_U32(session.session_id,
                  read_u32_le(&notification.payload[profile->range_data_primary_session_id_offset]),
                  "measurement notification primary session");
    ASSERT_EQ_U32(session.session_id,
                  read_u32_le(&notification.payload[profile->range_data_secondary_session_id_offset]),
                  "measurement notification secondary session");
    ASSERT_EQ_U32(profile->ranging_interval_ms,
                  read_u32_le(&notification.payload[profile->range_data_interval_offset]),
                  "measurement notification interval");
    ASSERT_EQ_U16((uint16_t)(profile->range_data_distance_base_cm +
                             (session.ranging_count * profile->range_data_distance_step_cm)),
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset]),
                  "measurement notification distance");
    PASS();
}

static void test_result_report_config_masks_range_notification_fields(void) {
    const uci_sim_profile_t* profile = uci_sim_default_profile();
    uci_sim_session_t session;
    uci_sim_measurement_t measurement;
    uci_sim_measurement_policy_result_t policy_result;
    uci_sim_packet_t notification;
    uint8_t result_report_config;

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;

    result_report_config = 0x01;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &result_report_config,
                                             1) == 0,
                "result report config store for tof-only failed");
    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 1U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);
    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "tof-only notification build failed");
    ASSERT_EQ_U16(profile->range_data_distance_base_cm,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset]),
                  "tof-only should preserve distance");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 2]),
                  "tof-only should suppress local azimuth");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 4],
                 "tof-only should suppress local azimuth fom");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 5]),
                  "tof-only should suppress local elevation");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 7],
                 "tof-only should suppress local elevation fom");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 8]),
                  "tof-only should suppress remote azimuth");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 10],
                 "tof-only should suppress remote azimuth fom");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 11]),
                  "tof-only should suppress remote elevation");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 13],
                 "tof-only should suppress remote elevation fom");

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    result_report_config = 0x07;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &result_report_config,
                                             1) == 0,
                "result report config store for tof+aoa failed");
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_AOA_RESULT_REQ,
                                             (const uint8_t[]){0x03},
                                             1) == 0,
                "result report config store for aoa both failed");
    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 2U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);
    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "tof+aoa notification build failed");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 2]) != 0,
                "tof+aoa should preserve local azimuth");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 5]) != 0,
                "tof+aoa should preserve local elevation");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 8]) != 0,
                "tof+aoa should preserve remote azimuth");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 11]) != 0,
                "tof+aoa should preserve remote elevation");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 4],
                 "tof+aoa should suppress local azimuth fom");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 7],
                 "tof+aoa should suppress local elevation fom");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 10],
                 "tof+aoa should suppress remote azimuth fom");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 13],
                 "tof+aoa should suppress remote elevation fom");
    PASS();
}

static void test_measurement_policy_serializes_session_ranging_interval_override(void) {
    const uci_sim_profile_t* profile = uci_sim_default_profile();
    uci_sim_session_t session;
    uci_sim_measurement_t measurement;
    uci_sim_measurement_policy_result_t policy_result;
    uci_sim_packet_t notification;
    const uint8_t interval_value[4] = { 0xB8, 0x0B, 0x00, 0x00 };

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    session.ranging_count = 1;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RANGING_INTERVAL,
                                             interval_value,
                                             sizeof(interval_value)) == 0,
                "measurement interval override store failed");
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             (const uint8_t[]){0x0F},
                                             1) == 0,
                "measurement interval result report config store failed");

    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 8U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);

    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "measurement interval notification build failed");
    ASSERT_EQ_U32(3000U,
                  read_u32_le(&notification.payload[profile->range_data_interval_offset]),
                  "measurement notification should use session interval override");
    PASS();
}

static void test_aoa_result_req_masks_range_notification_axes(void) {
    const uci_sim_profile_t* profile = uci_sim_default_profile();
    uci_sim_session_t session;
    uci_sim_measurement_t measurement;
    uci_sim_measurement_policy_result_t policy_result;
    uci_sim_packet_t notification;
    uint8_t result_report_config = 0x0F;
    uint8_t aoa_result_req;

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &result_report_config,
                                             1) == 0,
                "aoa-result result-report config store failed");

    aoa_result_req = 0x00;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_AOA_RESULT_REQ,
                                             &aoa_result_req,
                                             1) == 0,
                "aoa-result none store failed");
    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 3U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);
    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "aoa-result none notification build failed");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 2]),
                  "aoa-result none should suppress local azimuth");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 5]),
                  "aoa-result none should suppress local elevation");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 4],
                 "aoa-result none should suppress local azimuth fom");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 7],
                 "aoa-result none should suppress local elevation fom");

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &result_report_config,
                                             1) == 0,
                "aoa-result elevation result-report config store failed");
    aoa_result_req = 0x01;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_AOA_RESULT_REQ,
                                             &aoa_result_req,
                                             1) == 0,
                "aoa-result elevation store failed");
    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 4U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);
    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "aoa-result elevation notification build failed");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 2]),
                  "aoa-result elevation should suppress local azimuth");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 8]),
                  "aoa-result elevation should suppress remote azimuth");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 5]) != 0,
                "aoa-result elevation should preserve local elevation");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 11]) != 0,
                "aoa-result elevation should preserve remote elevation");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 4],
                 "aoa-result elevation should suppress azimuth fom");
    ASSERT_TRUE(notification.payload[profile->range_data_measurement_distance_offset + 7] != 0,
                "aoa-result elevation should preserve elevation fom");

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &result_report_config,
                                             1) == 0,
                "aoa-result azimuth result-report config store failed");
    aoa_result_req = 0x02;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_AOA_RESULT_REQ,
                                             &aoa_result_req,
                                             1) == 0,
                "aoa-result azimuth store failed");
    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 5U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);
    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "aoa-result azimuth notification build failed");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 2]) != 0,
                "aoa-result azimuth should preserve local azimuth");
    ASSERT_TRUE(read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 8]) != 0,
                "aoa-result azimuth should preserve remote azimuth");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 5]),
                  "aoa-result azimuth should suppress local elevation");
    ASSERT_EQ_U16(0,
                  read_u16_le(&notification.payload[profile->range_data_measurement_distance_offset + 11]),
                  "aoa-result azimuth should suppress remote elevation");
    ASSERT_TRUE(notification.payload[profile->range_data_measurement_distance_offset + 4] != 0,
                "aoa-result azimuth should preserve azimuth fom");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 7],
                 "aoa-result azimuth should suppress elevation fom");
    PASS();
}

static void test_rssi_reporting_masks_range_notification_rssi(void) {
    const uci_sim_profile_t* profile = uci_sim_default_profile();
    uci_sim_session_t session;
    uci_sim_measurement_t measurement;
    uci_sim_measurement_policy_result_t policy_result;
    uci_sim_packet_t notification;
    uint8_t result_report_config = 0x0F;
    uint8_t aoa_result_req = 0x03;
    uint8_t rssi_reporting = 0x00;

    memset(&session, 0, sizeof(session));
    session.session_id = 0x12345678U;
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &result_report_config,
                                             1) == 0,
                "rssi-report result-report config store failed");
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_AOA_RESULT_REQ,
                                             &aoa_result_req,
                                             1) == 0,
                "rssi-report aoa-result store failed");
    ASSERT_TRUE(uci_sim_session_store_config(&session,
                                             UCI_APP_CONFIG_RSSI_REPORTING,
                                             &rssi_reporting,
                                             1) == 0,
                "rssi-report disabled store failed");

    uci_sim_measurement_init_ranging_sample(profile, &session, &measurement);
    measurement.sequence_number = 6U;
    uci_sim_measurement_evaluate_range_notification_policy(&session, &measurement, &policy_result);
    ASSERT_TRUE(uci_sim_measurement_build_range_data_notification(profile,
                                                                  &measurement,
                                                                  &policy_result,
                                                                  profile->range_data_notification_oid,
                                                                  &notification) == 0,
                "rssi-report notification build failed");
    ASSERT_EQ_U8(0,
                 notification.payload[profile->range_data_measurement_distance_offset + 15],
                 "rssi-report disabled should suppress rssi");
    PASS();
}

static uci_sim_time_ms_t fake_clock_now_ms(void* context) {
    (void)context;
    return g_fake_clock_ms;
}

static int dequeue_outbound(uci_sim_engine_t* engine, uci_sim_packet_t* packet) {
    return uci_sim_engine_dequeue_outbound_packet(engine, packet);
}

static void test_engine_clock_poll_progression(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t packet;
    uci_sim_clock_t clock = { fake_clock_now_ms, NULL };

    g_fake_clock_ms = 1;
    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    uci_sim_engine_set_clock(&engine, &clock);
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine initial poll failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "engine clock init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock init rsp missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock init ntf missing");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "engine clock start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock start rsp missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock start ntf missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine clock range 1 should wait for interval");

    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine zero-delta poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine zero-delta poll should not emit range data");

    g_fake_clock_ms = 1U + engine.device.profile->ranging_interval_ms;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine first deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock range 1 missing");
    ASSERT_EQ_U32(1, read_u32_le(packet.payload), "engine clock range 1 sequence");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine clock range 2 should wait for next deadline");

    g_fake_clock_ms += engine.device.profile->ranging_interval_ms;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine second deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock range 2 missing");
    ASSERT_EQ_U32(2, read_u32_le(packet.payload), "engine clock range 2 sequence");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine clock range 3 should wait for next deadline");

    g_fake_clock_ms += engine.device.profile->ranging_interval_ms;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine third deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock range 3 missing");
    ASSERT_EQ_U32(3, read_u32_le(packet.payload), "engine clock range 3 sequence");
    PASS();
}

static void test_engine_uses_session_ranging_interval_override(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t packet;
    uci_sim_clock_t clock = { fake_clock_now_ms, NULL };
    const uci_sim_profile_t* profile = uci_sim_default_profile();

    g_fake_clock_ms = 1;
    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    uci_sim_engine_set_clock(&engine, &clock);
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine interval initial poll failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "engine interval init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval init rsp missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval init ntf missing");

    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 11;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_RANGING_INTERVAL;
    request.payload[6] = 0x04;
    request.payload[7] = 0xB8;
    request.payload[8] = 0x0B;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "engine interval set config failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval set config rsp missing");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "engine interval start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval start rsp missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval start ntf missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine interval range 1 should wait for session interval");

    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine interval zero-delta poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine interval zero-delta poll should not emit range data");

    g_fake_clock_ms = 3001U;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine interval first deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval range 1 missing");
    ASSERT_EQ_U32(1U, read_u32_le(packet.payload), "engine interval range 1 sequence");
    ASSERT_EQ_U32(3000U,
                  read_u32_le(&packet.payload[profile->range_data_interval_offset]),
                  "engine interval range 1 should use session interval");

    g_fake_clock_ms = 6000U;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine interval pre-deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine interval range 2 should wait for full override");

    g_fake_clock_ms = 6001U;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine interval second deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval range 2 missing");
    ASSERT_EQ_U32(2U, read_u32_le(packet.payload), "engine interval range 2 sequence");
    ASSERT_EQ_U32(3000U,
                  read_u32_le(&packet.payload[profile->range_data_interval_offset]),
                  "engine interval range 2 should use session interval");

    g_fake_clock_ms = 9001U;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine interval third deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine interval range 3 missing");
    ASSERT_EQ_U32(3U, read_u32_le(packet.payload), "engine interval range 3 sequence");
    ASSERT_EQ_U32(3000U,
                  read_u32_le(&packet.payload[profile->range_data_interval_offset]),
                  "engine interval range 3 should use session interval");
    PASS();
}

static void test_ranging_interval_validation_rejects_below_profile_min(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "interval validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "interval validation session lookup failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 11;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_RANGING_INTERVAL;
    request.payload[6] = 0x04;
    request.payload[7] = 49;
    request.payload[8] = 0x00;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "interval below minimum should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_RANGE, result.response.payload[0], "interval below minimum status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "interval below minimum config status count");
    ASSERT_TRUE(result.has_notification, "interval below minimum should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "interval below minimum generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_RANGE, result.notification.payload[0], "interval below minimum generic error status");
    ASSERT_EQ_U32(device.profile->ranging_interval_ms,
                  uci_sim_session_get_ranging_interval_ms(session, device.profile),
                  "invalid interval should not overwrite stored session interval");
    PASS();
}

static void test_session_start_rejects_invalid_ranging_interval(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_interval[4] = { 49, 0x00, 0x00, 0x00 };

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "interval start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "interval start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_RANGING_INTERVAL,
                                             invalid_interval,
                                             sizeof(invalid_interval)) == 0,
                "interval start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "start with invalid interval should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_RANGE, result.response.payload[0], "start invalid interval status");
    ASSERT_TRUE(result.has_notification, "start invalid interval should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid interval generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_RANGE, result.notification.payload[0], "start invalid interval generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid interval should preserve session state");
    PASS();
}

static void test_result_report_config_validation_rejects_unsupported_bits(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_result_report_config;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "result report validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "result report validation session lookup failed");
    original_result_report_config = uci_sim_session_get_result_report_config(session);

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_RESULT_REPORT_CONFIG;
    request.payload[6] = 0x01;
    request.payload[7] = 0x10;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "result report config unsupported bits should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "result report invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "result report invalid config status count");
    ASSERT_TRUE(result.has_notification, "result report invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "result report invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "result report invalid generic error status");
    ASSERT_EQ_U8(original_result_report_config,
                 uci_sim_session_get_result_report_config(session),
                 "invalid result report config should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_invalid_result_report_config(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_result_report_config = 0x10;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "result report start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "result report start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_RESULT_REPORT_CONFIG,
                                             &invalid_result_report_config,
                                             sizeof(invalid_result_report_config)) == 0,
                "result report start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid result report config should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid result report status");
    ASSERT_TRUE(result.has_notification, "start invalid result report should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid result report generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid result report generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid result report should preserve session state");
    PASS();
}

static void test_aoa_result_req_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_aoa_result_req;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "aoa result validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "aoa result validation session lookup failed");
    original_aoa_result_req = uci_sim_session_get_aoa_result_req(session);

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_AOA_RESULT_REQ;
    request.payload[6] = 0x01;
    request.payload[7] = 0x04;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "aoa result req unsupported value should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "aoa result invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "aoa result invalid config status count");
    ASSERT_TRUE(result.has_notification, "aoa result invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "aoa result invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "aoa result invalid generic error status");
    ASSERT_EQ_U8(original_aoa_result_req,
                 uci_sim_session_get_aoa_result_req(session),
                 "invalid aoa result req should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_invalid_aoa_result_req(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_aoa_result_req = 0x04;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "aoa result start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "aoa result start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_AOA_RESULT_REQ,
                                             &invalid_aoa_result_req,
                                             sizeof(invalid_aoa_result_req)) == 0,
                "aoa result start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid aoa result req should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid aoa result status");
    ASSERT_TRUE(result.has_notification, "start invalid aoa result should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid aoa result generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid aoa result generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid aoa result should preserve session state");
    PASS();
}

static void test_prf_mode_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_prf_mode = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "prf mode validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "prf mode validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_PRF_MODE, &original_prf_mode, &value_len) == 0,
                "prf mode validation fetch original value failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_PRF_MODE;
    request.payload[6] = 0x01;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported prf mode should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "prf mode invalid status");
    ASSERT_TRUE(result.has_notification, "prf mode invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "prf mode invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "prf mode invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_PRF_MODE, &original_prf_mode, &value_len) == 0,
                "prf mode validation refetch original value failed");
    ASSERT_EQ_U8(0x00, original_prf_mode,
                 "invalid prf mode should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_invalid_prf_mode(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_prf_mode = 0x03;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "prf mode start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "prf mode start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_PRF_MODE,
                                             &invalid_prf_mode,
                                             sizeof(invalid_prf_mode)) == 0,
                "prf mode start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid prf mode should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid prf mode status");
    ASSERT_TRUE(result.has_notification, "start invalid prf mode should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid prf mode generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid prf mode generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid prf mode should preserve session state");
    PASS();
}

static void test_preamble_code_index_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t preamble_code_index = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "preamble validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "preamble validation session lookup failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_PRF_MODE;
    request.payload[6] = 0x01;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set hprf prf mode failed");

    request.payload[5] = UCI_APP_CONFIG_PREAMBLE_CODE_INDEX;
    request.payload[7] = 0x19;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set valid hprf preamble failed");
    ASSERT_TRUE(uci_sim_session_get_config(session,
                                           UCI_APP_CONFIG_PREAMBLE_CODE_INDEX,
                                           &preamble_code_index,
                                           &value_len) == 0,
                "preamble validation fetch valid stored value failed");
    ASSERT_EQ_U8(1, value_len, "preamble validation stored value len");
    ASSERT_EQ_U8(0x19, preamble_code_index, "valid preamble should be stored");

    request.payload[7] = 0x18;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported preamble code index should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "preamble invalid status");
    ASSERT_TRUE(result.has_notification, "preamble invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "preamble invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "preamble invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session,
                                           UCI_APP_CONFIG_PREAMBLE_CODE_INDEX,
                                           &preamble_code_index,
                                           &value_len) == 0,
                "preamble validation refetch stored value failed");
    ASSERT_EQ_U8(0x19, preamble_code_index,
                 "invalid preamble code index should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_invalid_preamble_code_index(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t hprf_mode = 0x01;
    const uint8_t invalid_preamble_code_index = 0x18;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "preamble start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "preamble start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_PRF_MODE,
                                             &hprf_mode,
                                             sizeof(hprf_mode)) == 0,
                "preamble start validation prf preload failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_PREAMBLE_CODE_INDEX,
                                             &invalid_preamble_code_index,
                                             sizeof(invalid_preamble_code_index)) == 0,
                "preamble start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid preamble code index should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid preamble status");
    ASSERT_TRUE(result.has_notification, "start invalid preamble should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid preamble generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid preamble generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid preamble should preserve session state");
    PASS();
}

static void test_rssi_reporting_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_rssi_reporting;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "rssi reporting validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "rssi reporting validation session lookup failed");
    original_rssi_reporting = uci_sim_session_get_rssi_reporting(session);

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_RSSI_REPORTING;
    request.payload[6] = 0x01;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "rssi reporting unsupported value should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "rssi reporting invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "rssi reporting invalid config status count");
    ASSERT_TRUE(result.has_notification, "rssi reporting invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "rssi reporting invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "rssi reporting invalid generic error status");
    ASSERT_EQ_U8(original_rssi_reporting,
                 uci_sim_session_get_rssi_reporting(session),
                 "invalid rssi reporting should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_invalid_rssi_reporting(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_rssi_reporting = 0x02;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "rssi reporting start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "rssi reporting start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_RSSI_REPORTING,
                                             &invalid_rssi_reporting,
                                             sizeof(invalid_rssi_reporting)) == 0,
                "rssi reporting start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid rssi reporting should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid rssi reporting status");
    ASSERT_TRUE(result.has_notification, "start invalid rssi reporting should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid rssi reporting generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid rssi reporting generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid rssi reporting should preserve session state");
    PASS();
}

static void test_sts_config_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_sts_config = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "sts config validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "sts config validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_STS_CONFIG, &original_sts_config, &value_len) == 0,
                "sts config validation get original failed");
    ASSERT_EQ_U8(1, value_len, "sts config validation original length");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_STS_CONFIG;
    request.payload[6] = 0x01;
    request.payload[7] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported sts config should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "sts config invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "sts config invalid config status count");
    ASSERT_TRUE(result.has_notification, "sts config invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "sts config invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "sts config invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_STS_CONFIG, &original_sts_config, &value_len) == 0,
                "sts config validation get current failed");
    ASSERT_EQ_U8(0x01, original_sts_config, "invalid sts config should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_static_sts_without_iv(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t sts_config_static = 0x00;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "static sts start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "static sts start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_STS_CONFIG,
                                             &sts_config_static,
                                             sizeof(sts_config_static)) == 0,
                "static sts preload failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_STATIC_STS_IV,
                                             NULL,
                                             0) == 0,
                "static sts iv clear failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start without static sts iv should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start missing static sts iv status");
    ASSERT_TRUE(result.has_notification, "start missing static sts iv should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start missing static sts iv generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start missing static sts iv generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start missing static sts iv should preserve session state");
    PASS();
}

static void test_session_start_rejects_provisioned_sts_without_session_key(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t sts_config_provisioned = 0x03;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "provisioned sts start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "provisioned sts start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_STS_CONFIG,
                                             &sts_config_provisioned,
                                             sizeof(sts_config_provisioned)) == 0,
                "provisioned sts preload failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_SESSION_KEY,
                                             NULL,
                                             0) == 0,
                "session key clear failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start without provisioned sts session key should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start missing provisioned sts key status");
    ASSERT_TRUE(result.has_notification, "start missing provisioned sts key should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start missing provisioned sts key generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start missing provisioned sts key generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start missing provisioned sts key should preserve session state");
    PASS();
}

static void test_ranging_round_usage_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_ranging_round_usage;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "round usage validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "round usage validation session lookup failed");
    original_ranging_round_usage = uci_sim_session_get_ranging_round_usage(session);

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_RANGING_ROUND_USAGE;
    request.payload[6] = 0x01;
    request.payload[7] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported round usage should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "round usage invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "round usage invalid config status count");
    ASSERT_TRUE(result.has_notification, "round usage invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "round usage invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "round usage invalid generic error status");
    ASSERT_EQ_U8(original_ranging_round_usage,
                 uci_sim_session_get_ranging_round_usage(session),
                 "invalid round usage should not overwrite stored value");
    PASS();
}

static void test_device_type_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_device_type = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "device type validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "device type validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_DEVICE_TYPE, &original_device_type, &value_len) == 0,
                "device type validation fetch original device type failed");
    ASSERT_EQ_U8(1, value_len, "device type validation original device type len");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_DEVICE_TYPE;
    request.payload[6] = 0x01;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported device type should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "device type invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "device type invalid config status count");
    ASSERT_TRUE(result.has_notification, "device type invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "device type invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "device type invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_DEVICE_TYPE, &original_device_type, &value_len) == 0,
                "device type validation refetch original device type failed");
    ASSERT_EQ_U8(UCI_DEVICE_TYPE_CONTROLLER, original_device_type,
                 "invalid device type should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_device_type_role_mismatch(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t controlee_device_type = UCI_DEVICE_TYPE_CONTROLEE;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "device type mismatch init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "device type mismatch session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_DEVICE_TYPE,
                                             &controlee_device_type,
                                             sizeof(controlee_device_type)) == 0,
                "device type mismatch preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with device type role mismatch should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid device type mismatch status");
    ASSERT_TRUE(result.has_notification, "start invalid device type mismatch should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid device type mismatch generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid device type mismatch generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid device type mismatch should preserve session state");
    PASS();
}


static void test_multi_node_mode_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_multi_node_mode = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "multi node mode validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "multi node mode validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_MULTI_NODE_MODE, &original_multi_node_mode, &value_len) == 0,
                "multi node mode validation fetch original value failed");
    ASSERT_EQ_U8(1, value_len, "multi node mode validation original value len");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_MULTI_NODE_MODE;
    request.payload[6] = 0x01;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported multi node mode should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "multi node mode invalid status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "multi node mode invalid config status count");
    ASSERT_TRUE(result.has_notification, "multi node mode invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "multi node mode invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "multi node mode invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_MULTI_NODE_MODE, &original_multi_node_mode, &value_len) == 0,
                "multi node mode validation refetch original value failed");
    ASSERT_EQ_U8(0x01, original_multi_node_mode,
                 "invalid multi node mode should not overwrite stored value");
    PASS();
}

static void test_channel_number_validation_rejects_unsupported_values(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_channel_number = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "channel number validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "channel number validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_CHANNEL_NUMBER, &original_channel_number, &value_len) == 0,
                "channel number validation fetch original value failed");
    ASSERT_EQ_U8(1, value_len, "channel number validation original value len");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_CHANNEL_NUMBER;
    request.payload[6] = 0x01;
    request.payload[7] = 0x06;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported channel number should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "channel number invalid status");
    ASSERT_TRUE(result.has_notification, "channel number invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "channel number invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "channel number invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_CHANNEL_NUMBER, &original_channel_number, &value_len) == 0,
                "channel number validation refetch original value failed");
    ASSERT_EQ_U8(0x05, original_channel_number,
                 "invalid channel number should not overwrite stored value");
    PASS();
}

static void test_session_start_rejects_invalid_channel_number(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_channel_number = 0x06;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "channel number start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "channel number start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_CHANNEL_NUMBER,
                                             &invalid_channel_number,
                                             sizeof(invalid_channel_number)) == 0,
                "channel number start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid channel number should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid channel number status");
    ASSERT_TRUE(result.has_notification, "start invalid channel number should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid channel number generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid channel number generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid channel number should preserve session state");
    PASS();
}

static void test_session_start_rejects_unicast_multi_node_topology_mismatch(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t unicast_multi_node_mode = 0x00;
    const uint8_t invalid_controlee_count = 0x02;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "multi node mode topology init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "multi node mode topology session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_MULTI_NODE_MODE,
                                             &unicast_multi_node_mode,
                                             sizeof(unicast_multi_node_mode)) == 0,
                "multi node mode topology preload mode failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_NUMBER_OF_CONTROLEES,
                                             &invalid_controlee_count,
                                             sizeof(invalid_controlee_count)) == 0,
                "multi node mode topology preload count failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid unicast topology should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid multi node topology status");
    ASSERT_TRUE(result.has_notification, "start invalid multi node topology should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid multi node topology generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid multi node topology generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid multi node topology should preserve session state");
    PASS();
}

static void test_number_of_controlees_validation_rejects_excessive_value(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_number_of_controlees = 0x00;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "number_of_controlees validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "number_of_controlees validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_NUMBER_OF_CONTROLEES, &original_number_of_controlees, &value_len) == 0,
                "number_of_controlees validation fetch original value failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_NUMBER_OF_CONTROLEES;
    request.payload[6] = 0x01;
    request.payload[7] = 0x09;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported number_of_controlees should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "number_of_controlees invalid status");
    ASSERT_TRUE(result.has_notification, "number_of_controlees invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "number_of_controlees invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "number_of_controlees invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_NUMBER_OF_CONTROLEES, &original_number_of_controlees, &value_len) == 0,
                "number_of_controlees validation refetch original value failed");
    ASSERT_EQ_U8(0x03, original_number_of_controlees,
                 "invalid number_of_controlees should not overwrite stored value");
    PASS();
}

static void test_mac_address_mode_validation_rejects_unsupported_value(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_mac_address_mode = 0xFF;
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "mac_address_mode validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "mac_address_mode validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_MAC_ADDRESS_MODE, &original_mac_address_mode, &value_len) == 0,
                "mac_address_mode validation fetch original value failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_MAC_ADDRESS_MODE;
    request.payload[6] = 0x01;
    request.payload[7] = UCI_MAC_ADDRESS_MODE_EXTENDED;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "unsupported mac_address_mode should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "mac_address_mode invalid status");
    ASSERT_TRUE(result.has_notification, "mac_address_mode invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "mac_address_mode invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "mac_address_mode invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_MAC_ADDRESS_MODE, &original_mac_address_mode, &value_len) == 0,
                "mac_address_mode validation refetch original value failed");
    ASSERT_EQ_U8(UCI_MAC_ADDRESS_MODE_SHORT, original_mac_address_mode,
                 "invalid mac_address_mode should not overwrite stored value");
    PASS();
}

static void test_device_mac_address_validation_rejects_invalid_length(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_device_mac[UCI_SIM_MAX_CONFIG_VALUE] = {0};
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "device_mac_address validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "device_mac_address validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_DEVICE_MAC_ADDRESS, original_device_mac, &value_len) == 0,
                "device_mac_address validation fetch original value failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 11;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_DEVICE_MAC_ADDRESS;
    request.payload[6] = 0x04;
    request.payload[7] = 0xAA;
    request.payload[8] = 0xBB;
    request.payload[9] = 0xCC;
    request.payload[10] = 0xDD;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "invalid device_mac_address length should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "device_mac_address invalid status");
    ASSERT_TRUE(result.has_notification, "device_mac_address invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "device_mac_address invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "device_mac_address invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_DEVICE_MAC_ADDRESS, original_device_mac, &value_len) == 0,
                "device_mac_address validation refetch original value failed");
    ASSERT_EQ_U8(2, value_len, "invalid device_mac_address should preserve original length");
    ASSERT_EQ_U8(0xCD, original_device_mac[0], "invalid device_mac_address should preserve original first byte");
    ASSERT_EQ_U8(0xAB, original_device_mac[1], "invalid device_mac_address should preserve original second byte");
    PASS();
}

static void test_dst_mac_address_validation_rejects_invalid_list_length(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t original_dst_mac[UCI_SIM_MAX_CONFIG_VALUE] = {0};
    uint8_t value_len = 0;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dst_mac_address validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "dst_mac_address validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_DST_MAC_ADDRESS, original_dst_mac, &value_len) == 0,
                "dst_mac_address validation fetch original value failed");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 10;
    request.payload[4] = 0x01;
    request.payload[5] = UCI_APP_CONFIG_DST_MAC_ADDRESS;
    request.payload[6] = 0x03;
    request.payload[7] = 0x78;
    request.payload[8] = 0x56;
    request.payload[9] = 0x34;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "invalid dst_mac_address list should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "dst_mac_address invalid status");
    ASSERT_TRUE(result.has_notification, "dst_mac_address invalid should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "dst_mac_address invalid generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "dst_mac_address invalid generic error status");
    ASSERT_TRUE(uci_sim_session_get_config(session, UCI_APP_CONFIG_DST_MAC_ADDRESS, original_dst_mac, &value_len) == 0,
                "dst_mac_address validation refetch original value failed");
    ASSERT_EQ_U8(6, value_len, "invalid dst_mac_address should preserve original length");
    ASSERT_EQ_U8(0x78, original_dst_mac[0], "invalid dst_mac_address should preserve original first byte");
    ASSERT_EQ_U8(0x56, original_dst_mac[1], "invalid dst_mac_address should preserve original second byte");
    PASS();
}

static void test_session_start_rejects_controlee_count_dst_list_mismatch(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t one_to_many_multi_node_mode = 0x01;
    const uint8_t invalid_controlee_count = 0x02;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "count/list mismatch init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "count/list mismatch session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_MULTI_NODE_MODE,
                                             &one_to_many_multi_node_mode,
                                             sizeof(one_to_many_multi_node_mode)) == 0,
                "count/list mismatch preload mode failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_NUMBER_OF_CONTROLEES,
                                             &invalid_controlee_count,
                                             sizeof(invalid_controlee_count)) == 0,
                "count/list mismatch preload count failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with count/list mismatch should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid count/list mismatch status");
    ASSERT_TRUE(result.has_notification, "start invalid count/list mismatch should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid count/list mismatch generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid count/list mismatch generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid count/list mismatch should preserve session state");
    PASS();
}

static void test_session_start_rejects_invalid_ranging_round_usage(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    const uint8_t invalid_ranging_round_usage = 0x05;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "round usage start validation init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "round usage start validation session lookup failed");
    ASSERT_TRUE(uci_sim_session_store_config(session,
                                             UCI_APP_CONFIG_RANGING_ROUND_USAGE,
                                             &invalid_ranging_round_usage,
                                             sizeof(invalid_ranging_round_usage)) == 0,
                "round usage start validation preload failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0,
                "start with invalid round usage should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "start invalid round usage status");
    ASSERT_TRUE(result.has_notification, "start invalid round usage should emit generic error ntf");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "start invalid round usage generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "start invalid round usage generic error status");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, session->state, "start invalid round usage should preserve session state");
    PASS();
}

static void test_packet_round_trip(void) {
    uint8_t bytes[UCI_SIM_MAX_PACKET];
    size_t written = 0;
    uci_sim_packet_t packet;
    uci_sim_packet_t parsed;

    memset(&packet, 0, sizeof(packet));
    packet.mt = UCI_MT_COMMAND;
    packet.pbf = UCI_PBF_COMPLETE;
    packet.gid = UCI_GID_CORE;
    packet.oid = UCI_CORE_DEVICE_INFO;
    packet.payload_len = 2;
    packet.payload[0] = 0xAA;
    packet.payload[1] = 0x55;

    ASSERT_TRUE(uci_sim_packet_serialize(&packet, bytes, sizeof(bytes), &written) == 0, "serialize failed");
    ASSERT_TRUE(uci_sim_packet_parse(bytes, written, &parsed) == 0, "parse failed");
    ASSERT_EQ_U8(packet.mt, parsed.mt, "mt mismatch");
    ASSERT_EQ_U8(packet.gid, parsed.gid, "gid mismatch");
    ASSERT_EQ_U8(packet.oid, parsed.oid, "oid mismatch");
    ASSERT_EQ_U8(packet.payload[0], parsed.payload[0], "payload mismatch");
    PASS();
}

static void test_core_device_info(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_DEVICE_INFO;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "device info handle failed");
    ASSERT_TRUE(result.has_response, "device info missing response");
    ASSERT_EQ_U8(UCI_MT_RESPONSE, result.response.mt, "device info mt");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "device info status");
    PASS();
}

static void test_default_profile_is_applied(void) {
    uci_sim_device_t device;
    const uci_sim_profile_t* profile = uci_sim_default_profile();

    uci_sim_device_init(&device);
    ASSERT_TRUE(device.profile == profile, "default profile pointer");
    ASSERT_EQ_U8(profile->default_device_state, device.device_state, "default profile device state");
    ASSERT_EQ_U8(profile->default_low_power_mode, device.device_configs[1].value[0], "default profile low power");
    ASSERT_EQ_U8(profile->default_device_pan_id[0], device.device_configs[2].value[0], "default profile pan lo");
    ASSERT_EQ_U8(profile->default_device_pan_id[1], device.device_configs[2].value[1], "default profile pan hi");
    PASS();
}

static void test_default_profile_feature_matrix(void) {
    const uci_sim_profile_t* profile = uci_sim_default_profile();
    const uci_sim_session_transition_t* start_transition =
        uci_sim_profile_get_session_transition(profile, UCI_SESSION_START);
    const uci_sim_session_transition_t* stop_transition =
        uci_sim_profile_get_session_transition(profile, UCI_SESSION_STOP);

    ASSERT_TRUE(uci_sim_profile_supports_command(profile, UCI_GID_CORE, UCI_CORE_DEVICE_INFO),
                "profile should support core device info");
    ASSERT_TRUE(uci_sim_profile_supports_command(profile, UCI_GID_SESSION_CONFIG, UCI_SESSION_INIT),
                "profile should support session init");
    ASSERT_TRUE(uci_sim_profile_supports_command(profile, UCI_GID_SESSION_CONTROL, UCI_SESSION_START),
                "profile should support session start");
    ASSERT_TRUE(uci_sim_profile_supports_command(profile, UCI_GID_CORE, UCI_CORE_DEVICE_RESET),
                "profile should support core reset");
    ASSERT_TRUE(uci_sim_profile_supports_core_config(profile, UCI_DEVICE_CONFIG_DEVICE_STATE),
                "profile should support device state config");
    ASSERT_TRUE(!uci_sim_profile_supports_core_config(profile, 0x7FU),
                "profile should reject unknown core config");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x00),
                "profile should support app config 0x00");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x04),
                "profile should support app config 0x04");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x05),
                "profile should support app config 0x05");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x06),
                "profile should support app config 0x06");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x07),
                "profile should support app config 0x07");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x08),
                "profile should support app config 0x08");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x09),
                "profile should support app config 0x09");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0A),
                "profile should support app config 0x0A");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0B),
                "profile should support app config 0x0B");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0C),
                "profile should support app config 0x0C");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0D),
                "profile should support app config 0x0D");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0E),
                "profile should support app config 0x0E");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0F),
                "profile should support app config 0x0F");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x10),
                "profile should support app config 0x10");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x12),
                "profile should support app config 0x12");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x13),
                "profile should support app config 0x13");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x14),
                "profile should support app config 0x14");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x15),
                "profile should support app config 0x15");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x16),
                "profile should support app config 0x16");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x17),
                "profile should support app config 0x17");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x18),
                "profile should support app config 0x18");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x19),
                "profile should support app config 0x19");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x1A),
                "profile should support app config 0x1A");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x1B),
                "profile should support app config 0x1B");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x26),
                "profile should support app config 0x26");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x27),
                "profile should support app config 0x27");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x28),
                "profile should support app config 0x28");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x29),
                "profile should support app config 0x29");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2A),
                "profile should support app config 0x2A");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2B),
                "profile should support app config 0x2B");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2C),
                "profile should support app config 0x2C");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2D),
                "profile should support app config 0x2D");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2E),
                "profile should support app config 0x2E");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2F),
                "profile should support app config 0x2F");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x30),
                "profile should support app config 0x30");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x31),
                "profile should support app config 0x31");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x32),
                "profile should support app config 0x32");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x33),
                "profile should support app config 0x33");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x3A),
                "profile should support app config 0x3A");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x3B),
                "profile should support app config 0x3B");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x3C),
                "profile should support app config 0x3C");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x3D),
                "profile should support app config 0x3D");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x3E),
                "profile should support app config 0x3E");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x3F),
                "profile should support app config 0x3F");
    ASSERT_TRUE(!uci_sim_profile_supports_session_app_config(profile, 0x99),
                "profile should reject unknown app config");
    ASSERT_TRUE(start_transition != NULL, "profile should define session start transition");
    ASSERT_TRUE(stop_transition != NULL, "profile should define session stop transition");
    ASSERT_TRUE(uci_sim_profile_supports_command(profile, UCI_GID_CORE, UCI_CORE_DEVICE_RESET),
                "profile should support device reset");
    ASSERT_TRUE(uci_sim_profile_supports_command(profile, UCI_GID_CORE, UCI_CORE_QUERY_UWBS_TIMESTAMP),
                "profile should support query timestamp");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, profile->initial_session_state, "profile initial session state");
    ASSERT_EQ_U8(UCI_SESSION_STATE_ACTIVE, start_transition->next_state, "profile start next state");
    ASSERT_EQ_U8(UCI_STATUS_REJECTED, start_transition->invalid_status, "profile invalid start status");
    ASSERT_EQ_U8(UCI_SESSION_START, profile->range_data_notification_oid, "profile range data oid");
    ASSERT_EQ_U8(3, profile->ranging_stream_burst_count, "profile range data burst count");
    ASSERT_EQ_U8(52, profile->range_data_payload_len, "profile range data payload len");
    ASSERT_EQ_U8(0x12, profile->range_data_payload_template[25], "profile range data short addr lo");
    ASSERT_EQ_U8(0x34, profile->range_data_payload_template[26], "profile range data short addr hi");
    ASSERT_TRUE(profile->initial_uwbs_timestamp == 0x1122334455667788ULL,
                "profile initial timestamp");
    ASSERT_TRUE(profile->uwbs_timestamp_increment == 1ULL,
                "profile timestamp increment");
    PASS();
}

static void test_core_device_config_storage(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_SET_CONFIG;
    request.payload_len = 4;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_DEVICE_STATE;
    request.payload[2] = 1;
    request.payload[3] = UCI_DEVICE_STATE_ACTIVE;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set core config failed");
    ASSERT_TRUE(result.has_response, "set core config response missing");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "set core config status");
    ASSERT_EQ_U8(UCI_DEVICE_STATE_ACTIVE, device.device_state, "device state not updated");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_GET_CONFIG;
    request.payload_len = 2;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_DEVICE_STATE;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get core config failed");
    ASSERT_TRUE(result.has_response, "get core config response missing");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get core config status");
    ASSERT_EQ_U8(1, result.response.payload[1], "get core config count");
    ASSERT_EQ_U8(UCI_DEVICE_CONFIG_DEVICE_STATE, result.response.payload[2], "get core config id");
    ASSERT_EQ_U8(1, result.response.payload[3], "get core config len");
    ASSERT_EQ_U8(UCI_DEVICE_STATE_ACTIVE, result.response.payload[4], "get core config value");
    PASS();
}

static void test_core_additional_device_configs(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_SET_CONFIG;
    request.payload_len = 4;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_LOW_POWER_MODE;
    request.payload[2] = 1;
    request.payload[3] = 1;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set low power mode failed");

    request.oid = UCI_CORE_GET_CONFIG;
    request.payload_len = 2;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_LOW_POWER_MODE;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get low power mode failed");
    ASSERT_EQ_U8(UCI_DEVICE_CONFIG_LOW_POWER_MODE, result.response.payload[2], "get low power mode id");
    ASSERT_EQ_U8(1, result.response.payload[4], "get low power mode value");

    request.oid = UCI_CORE_SET_CONFIG;
    request.payload_len = 5;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_DEVICE_PAN_ID;
    request.payload[2] = 2;
    request.payload[3] = 0x34;
    request.payload[4] = 0x12;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set device pan id failed");

    request.oid = UCI_CORE_GET_CONFIG;
    request.payload_len = 2;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_DEVICE_PAN_ID;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get device pan id failed");
    ASSERT_EQ_U8(UCI_DEVICE_CONFIG_DEVICE_PAN_ID, result.response.payload[2], "get device pan id id");
    ASSERT_EQ_U8(2, result.response.payload[3], "get device pan id len");
    ASSERT_EQ_U8(0x34, result.response.payload[4], "get device pan id lo");
    ASSERT_EQ_U8(0x12, result.response.payload[5], "get device pan id hi");
    PASS();
}

static void test_profile_rejects_unsupported_core_features(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_GENERIC_ERROR;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "unsupported core command should fail");
    ASSERT_EQ_U8(UCI_STATUS_UNKNOWN_OID, result.response.payload[0], "unsupported core command status");
    ASSERT_TRUE(result.has_notification, "unsupported core command should emit generic error notification");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "unsupported core command generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_UNKNOWN_OID, result.notification.payload[0], "unsupported core command generic error status");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_SET_CONFIG;
    request.payload_len = 4;
    request.payload[0] = 1;
    request.payload[1] = 0x7F;
    request.payload[2] = 1;
    request.payload[3] = 0x01;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "unsupported core config set should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "unsupported core config status");
    ASSERT_TRUE(result.has_notification, "unsupported core config should emit generic error notification");
    ASSERT_EQ_U8(UCI_CORE_GENERIC_ERROR, result.notification.oid, "unsupported core config generic error oid");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.notification.payload[0], "unsupported core config generic error status");
    PASS();
}

static void test_core_caps_match_profile(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    const uci_sim_profile_t* profile = uci_sim_default_profile();

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_GET_CAPS_INFO;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "core caps handle failed");
    ASSERT_EQ_U8(profile->core_caps_payload_len, result.response.payload_len, "core caps profile len");
    ASSERT_EQ_U8(profile->core_caps_payload[0], result.response.payload[0], "core caps profile status");
    ASSERT_EQ_U8(profile->core_caps_payload[1], result.response.payload[1], "core caps profile count");
    ASSERT_EQ_U8(profile->core_caps_payload[2], result.response.payload[2], "core caps profile tlv");
    ASSERT_EQ_U8(profile->core_caps_payload[3], result.response.payload[3], "core caps profile len byte");
    PASS();
}

static void test_core_query_timestamp_response(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    const uci_sim_profile_t* profile = uci_sim_default_profile();

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_QUERY_UWBS_TIMESTAMP;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "query timestamp first failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "query timestamp first status");
    ASSERT_TRUE(read_u64_le(&result.response.payload[1]) == profile->initial_uwbs_timestamp,
                "query timestamp first value");

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "query timestamp second failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "query timestamp second status");
    ASSERT_TRUE(read_u64_le(&result.response.payload[1]) ==
                (profile->initial_uwbs_timestamp + profile->uwbs_timestamp_increment),
                "query timestamp second value");
    PASS();
}

static void test_core_device_reset_restores_profile_defaults(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_SET_CONFIG;
    request.payload_len = 4;
    request.payload[0] = 1;
    request.payload[1] = UCI_DEVICE_CONFIG_DEVICE_STATE;
    request.payload[2] = 1;
    request.payload[3] = UCI_DEVICE_STATE_ACTIVE;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set state before reset failed");

    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session init before reset failed");

    request.gid = UCI_GID_CORE;
    request.oid = UCI_CORE_QUERY_UWBS_TIMESTAMP;
    request.payload_len = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "timestamp before reset failed");
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "timestamp increment before reset failed");

    request.oid = UCI_CORE_DEVICE_RESET;
    request.payload_len = 1;
    request.payload[0] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "device reset failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "device reset status");
    ASSERT_TRUE(result.has_notification, "device reset notification missing");
    ASSERT_EQ_U8(UCI_GID_CORE, result.notification.gid, "device reset notification gid");
    ASSERT_EQ_U8(UCI_CORE_DEVICE_STATUS_NTF, result.notification.oid, "device reset notification oid");
    ASSERT_EQ_U8(UCI_DEVICE_STATE_READY, result.notification.payload[0], "device reset ready state");
    ASSERT_EQ_U8(UCI_DEVICE_STATE_READY, device.device_state, "device state reset");
    ASSERT_EQ_U8(0, (uint8_t)device.sessions[0].allocated, "sessions cleared on reset");
    ASSERT_EQ_U8(0, (uint8_t)device.scheduled_event_count, "events cleared on reset");
    ASSERT_EQ_U8(0, (uint8_t)device.pending_notification_count, "pending notifications cleared on reset");

    request.oid = UCI_CORE_QUERY_UWBS_TIMESTAMP;
    request.payload_len = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "timestamp after reset failed");
    ASSERT_TRUE(read_u64_le(&result.response.payload[1]) == uci_sim_default_profile()->initial_uwbs_timestamp,
                "timestamp reset to profile default");
    PASS();
}

static void test_session_get_count(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_GET_COUNT;
    request.payload_len = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session get count empty failed");
    ASSERT_EQ_U8(0, result.response.payload[1], "session get count initial");

    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session init for count failed");

    request.oid = UCI_SESSION_GET_COUNT;
    request.payload_len = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session get count after init failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "session get count status");
    ASSERT_EQ_U8(1, result.response.payload[1], "session get count after init");
    PASS();
}

static void test_session_query_data_size_and_ranging_count(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session init for data size failed");

    request.oid = UCI_SESSION_QUERY_DATA_SIZE_IN_RANGING;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "query data size failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "query data size status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "query data size lo");
    ASSERT_EQ_U8(0x02, result.response.payload[2], "query data size hi");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_GET_RANGING_COUNT;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get ranging count failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get ranging count status");
    ASSERT_EQ_U8(0x00, result.response.payload[1], "get ranging count b0");
    ASSERT_EQ_U8(0x00, result.response.payload[2], "get ranging count b1");
    ASSERT_EQ_U8(0x00, result.response.payload[3], "get ranging count b2");
    ASSERT_EQ_U8(0x00, result.response.payload[4], "get ranging count b3");
    PASS();
}

static void test_ranging_stream_scenario(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t queued;
    uci_sim_packet_t response;
    uci_sim_packet_t notification;
    const uci_sim_profile_t* profile;
    uint32_t sequence;

    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    profile = engine.device.profile;
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging stream init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging stream init response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging stream init notification missing");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging stream start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging stream start response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging stream status notification missing");
    ASSERT_EQ_U8(UCI_GID_SESSION_CONFIG, notification.gid, "ranging stream status gid");
    ASSERT_EQ_U8(UCI_SESSION_STATUS_NTF, notification.oid, "ranging stream status oid");
    ASSERT_TRUE(dequeue_outbound(&engine, &queued) != 0, "ranging stream first range should wait for interval");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.pending_notification_count, "ranging stream pending drained after start");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.sessions[0].ranging_count, "ranging stream count after start");
    ASSERT_EQ_U8(profile->ranging_stream_burst_count,
                 engine.device.sessions[0].ranging_stream_remaining,
                 "ranging stream remaining after start");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after start");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, profile->ranging_interval_ms) == 0,
                "ranging stream first interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &queued) == 0, "ranging stream first range notification missing");
    ASSERT_EQ_U8(UCI_GID_SESSION_CONTROL, queued.gid, "ranging stream data gid");
    ASSERT_EQ_U8(profile->range_data_notification_oid, queued.oid, "ranging stream data oid");
    ASSERT_EQ_U8(profile->range_data_payload_len, (uint8_t)queued.payload_len, "ranging stream payload len");
    ASSERT_EQ_U8(profile->range_data_payload_template[24], queued.payload[24], "ranging stream measurement count");
    sequence = read_u32_le(queued.payload);
    ASSERT_EQ_U32(1, sequence, "ranging stream sequence 1");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.sessions[0].ranging_count, "ranging stream count after first interval");
    ASSERT_EQ_U8(profile->ranging_stream_burst_count - 1,
                 engine.device.sessions[0].ranging_stream_remaining,
                 "ranging stream remaining after first interval");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after first interval");
    ASSERT_EQ_U8(profile->range_data_payload_template[25], queued.payload[25], "ranging stream short addr lo");
    ASSERT_EQ_U8(profile->range_data_payload_template[26], queued.payload[26], "ranging stream short addr hi");
    ASSERT_EQ_U8((uint8_t)(profile->range_data_distance_base_cm & 0xFFU),
                 queued.payload[profile->range_data_measurement_distance_offset],
                 "ranging stream distance lo");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, profile->ranging_interval_ms) == 0,
                "ranging stream second interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging stream second range notification missing");
    ASSERT_EQ_U8(UCI_GID_SESSION_CONTROL, notification.gid, "ranging stream second gid");
    ASSERT_EQ_U8(UCI_SESSION_START, notification.oid, "ranging stream second oid");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(2, sequence, "ranging stream sequence 2");
    ASSERT_EQ_U8(2, (uint8_t)engine.device.sessions[0].ranging_count, "ranging stream count after second interval");
    ASSERT_EQ_U8(1, engine.device.sessions[0].ranging_stream_remaining, "ranging stream remaining after second interval");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after second interval");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_STOP;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging stream stop failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging stream stop response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging stream stop notification missing");
    ASSERT_EQ_U8(UCI_SESSION_STATUS_NTF, notification.oid, "ranging stream stop status oid");
    ASSERT_EQ_U8(UCI_SESSION_STATE_IDLE, notification.payload[4], "ranging stream stop state");
    ASSERT_EQ_U8(0, engine.device.sessions[0].ranging_stream_remaining, "ranging stream remaining after stop");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after stop");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_GET_RANGING_COUNT;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging stream get count failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging stream count response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging stream should not emit after stop");
    ASSERT_EQ_U32(2, read_u32_le(&response.payload[1]), "ranging stream final count");
    PASS();
}


static void test_ranging_stream_respects_info_ntf_disable(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t response;
    uci_sim_packet_t notification;

    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging disable init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging disable init response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging disable init notification missing");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[4] = 1;
    request.payload[5] = UCI_APP_CONFIG_SESSION_INFO_NTF_CONFIG;
    request.payload[6] = 1;
    request.payload[7] = 0x00;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging disable set_app_config failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging disable set_app_config response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging disable set_app_config should not notify");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging disable start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging disable start response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging disable status notification missing");
    ASSERT_EQ_U8(UCI_SESSION_STATUS_NTF, notification.oid, "ranging disable status oid");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging disable should suppress range data after start");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.sessions[0].ranging_count, "ranging disable count after start");
    ASSERT_EQ_U8(engine.device.profile->ranging_stream_burst_count,
                 engine.device.sessions[0].ranging_stream_remaining,
                 "ranging disable remaining after start");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "ranging disable first async tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging disable should suppress first interval range data");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.sessions[0].ranging_count, "ranging disable count after first interval");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "ranging disable second async tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging disable should suppress second interval range data");
    ASSERT_EQ_U8(2, (uint8_t)engine.device.sessions[0].ranging_count, "ranging disable count after second interval");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "ranging disable third async tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging disable should suppress third interval range data");
    ASSERT_EQ_U8(3, (uint8_t)engine.device.sessions[0].ranging_count, "ranging disable count after third interval");
    ASSERT_EQ_U8(0, engine.device.sessions[0].ranging_stream_remaining, "ranging disable remaining after third interval");
    PASS();
}

static void test_ranging_stream_respects_proximity_inside_mode(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t response;
    uci_sim_packet_t notification;

    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "proximity inside init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "proximity inside init response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity inside init notification missing");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 16;
    request.payload[4] = 3;
    request.payload[5] = UCI_APP_CONFIG_SESSION_INFO_NTF_CONFIG;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    request.payload[8] = UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_NEAR;
    request.payload[9] = 2;
    request.payload[10] = 100;
    request.payload[11] = 0;
    request.payload[12] = UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_FAR;
    request.payload[13] = 2;
    request.payload[14] = 105;
    request.payload[15] = 0;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "proximity inside set_app_config failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "proximity inside set_app_config response missing");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "proximity inside start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "proximity inside start response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity inside status notification missing");
    ASSERT_EQ_U8(UCI_SESSION_STATUS_NTF, notification.oid, "proximity inside status oid");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "proximity inside first range should wait for interval");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "proximity inside first interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity inside first range notification missing");
    ASSERT_EQ_U8(UCI_SESSION_START, notification.oid, "proximity inside first range oid");
    ASSERT_EQ_U16(100, read_u16_le(&notification.payload[engine.device.profile->range_data_measurement_distance_offset]),
                  "proximity inside first range distance");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "proximity inside second interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity inside second range notification missing");
    ASSERT_EQ_U16(105, read_u16_le(&notification.payload[engine.device.profile->range_data_measurement_distance_offset]),
                  "proximity inside second range distance");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "proximity inside third interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "proximity inside should suppress out-of-range notification");
    ASSERT_EQ_U8(3, (uint8_t)engine.device.sessions[0].ranging_count, "proximity inside count after third interval");
    PASS();
}

static void test_ranging_stream_respects_proximity_transition_mode(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t response;
    uci_sim_packet_t notification;
    uint32_t sequence;

    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "proximity transition init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "proximity transition init response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity transition init notification missing");

    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 16;
    request.payload[4] = 3;
    request.payload[5] = UCI_APP_CONFIG_SESSION_INFO_NTF_CONFIG;
    request.payload[6] = 1;
    request.payload[7] = 0x05;
    request.payload[8] = UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_NEAR;
    request.payload[9] = 2;
    request.payload[10] = 103;
    request.payload[11] = 0;
    request.payload[12] = UCI_APP_CONFIG_RNG_DATA_NTF_PROXIMITY_FAR;
    request.payload[13] = 2;
    request.payload[14] = 108;
    request.payload[15] = 0;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "proximity transition set_app_config failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "proximity transition set_app_config response missing");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "proximity transition start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "proximity transition start response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity transition status notification missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "proximity transition should not notify on start");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "proximity transition first interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "proximity transition should suppress baseline interval");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "proximity transition second interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity transition entering notification missing");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(1, sequence, "proximity transition entering sequence");
    ASSERT_EQ_U16(105, read_u16_le(&notification.payload[engine.device.profile->range_data_measurement_distance_offset]),
                  "proximity transition entering distance");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "proximity transition third interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "proximity transition leaving notification missing");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(2, sequence, "proximity transition leaving sequence");
    ASSERT_EQ_U16(110, read_u16_le(&notification.payload[engine.device.profile->range_data_measurement_distance_offset]),
                  "proximity transition leaving distance");
    PASS();
}

static void test_ranging_stream_progresses_to_completion(void) {
    uci_sim_engine_t engine;
    uci_sim_packet_t request;
    uci_sim_packet_t response;
    uci_sim_packet_t notification;
    uint32_t sequence;

    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_RANGING_STREAM);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging progression init failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging progression init response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression init notification missing");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging progression start failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging progression start response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression start notification missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging progression first range should wait for interval");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging progression scheduled after start");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "ranging progression first interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression first notification missing");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(1, sequence, "ranging progression sequence 1");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging progression scheduled after first");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "ranging progression second interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression second notification missing");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(2, sequence, "ranging progression sequence 2");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging progression scheduled after second");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, engine.device.profile->ranging_interval_ms) == 0,
                "ranging progression third interval tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression third notification missing");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(3, sequence, "ranging progression sequence 3");
    ASSERT_EQ_U8(0, engine.device.sessions[0].ranging_stream_remaining, "ranging progression remaining");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.scheduled_event_count, "ranging progression scheduled after completion");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_GET_RANGING_COUNT;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging progression get count failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging progression count response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) != 0, "ranging progression should complete cleanly");
    ASSERT_EQ_U32(3, read_u32_le(&response.payload[1]), "ranging progression final count");
    PASS();
}

static void test_default_scenario_initialization(void) {
    uci_sim_device_t device;

    uci_sim_device_init(&device);
    ASSERT_EQ_U8(UCI_SIM_SCENARIO_DEFAULT, device.scenario, "default scenario");
    PASS();
}

static void test_delayed_notification_scenario(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    const uint32_t session_id = 0x12345678U;

    uci_sim_device_init_with_scenario(&device, UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "delayed session init failed");
    ASSERT_TRUE(result.has_response, "delayed session init response missing");
    ASSERT_TRUE(!result.has_notification, "delayed session init should defer notification");

    request.oid = UCI_SESSION_GET_STATE;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "delayed get state failed");
    ASSERT_TRUE(result.has_response, "delayed get state response missing");
    ASSERT_TRUE(result.has_notification, "delayed notification missing on next command");
    ASSERT_EQ_U32(session_id, read_u32_le(result.notification.payload), "delayed notification session");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, result.notification.payload[4], "delayed notification state");
    PASS();
}

static int response_contains_config_tlv(const uci_sim_packet_t* response,
                                        uint8_t config_id,
                                        const uint8_t* expected_value,
                                        uint8_t expected_value_len) {
    size_t offset = 2;

    if (!response || response->payload_len < 2) {
        return 0;
    }

    while (offset + 2 <= response->payload_len) {
        uint8_t id = response->payload[offset++];
        uint8_t value_len = response->payload[offset++];

        if (offset + value_len > response->payload_len) {
            return 0;
        }
        if (id == config_id && value_len == expected_value_len) {
            if (expected_value_len == 0 ||
                memcmp(&response->payload[offset], expected_value, expected_value_len) == 0) {
                return 1;
            }
        }
        offset += value_len;
    }

    return 0;
}

static void test_session_app_config_storage(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "app config init failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 12;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 1;
    request.payload[5] = 0x00;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set app config failed");
    ASSERT_TRUE(result.has_response, "set app config response missing");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "set app config status");
    ASSERT_EQ_U8(0, result.response.payload[1], "set app config count");
    ASSERT_EQ_U8(2, result.response.payload_len, "set app config response length");

    request.payload[5] = 0x03;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set second app config failed");

    request.payload[5] = 0x11;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set third app config failed");

    request.payload[5] = 0x04;
    request.payload[7] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set channel app config failed");

    request.payload[5] = 0x05;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set no_of_controlee app config failed");

    request.payload[5] = 0x01;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ranging usage app config failed");

    request.payload[5] = 0x02;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set sts config app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x06;
    request.payload[6] = 2;
    request.payload[7] = 0xCD;
    request.payload[8] = 0xAB;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set device mac app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x07;
    request.payload[6] = 2;
    request.payload[7] = 0x78;
    request.payload[8] = 0x56;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dst mac app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x08;
    request.payload[6] = 2;
    request.payload[7] = 0x60;
    request.payload[8] = 0x09;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set slot duration app config failed");

    request.payload_len = 11;
    request.payload[5] = 0x09;
    request.payload[6] = 4;
    request.payload[7] = 0xD0;
    request.payload[8] = 0x07;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ranging duration app config failed");

    request.payload[5] = 0x0A;
    request.payload[6] = 4;
    request.payload[7] = 0x05;
    request.payload[8] = 0x00;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set sts index app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x0B;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set mac fcs type app config failed");

    request.payload[5] = 0x0C;
    request.payload[6] = 1;
    request.payload[7] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ranging round control app config failed");

    request.payload[5] = 0x0D;
    request.payload[6] = 1;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set aoa result req app config failed");

    request.payload[5] = 0x0E;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rng data ntf app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x0F;
    request.payload[6] = 2;
    request.payload[7] = 0x64;
    request.payload[8] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rng data ntf proximity near app config failed");

    request.payload[5] = 0x10;
    request.payload[6] = 2;
    request.payload[7] = 0xF4;
    request.payload[8] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rng data ntf proximity far app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x12;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rframe config app config failed");

    request.payload[5] = 0x13;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rssi reporting app config failed");

    request.payload[5] = 0x14;
    request.payload[6] = 1;
    request.payload[7] = 0x0C;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set preamble code index app config failed");

    request.payload[5] = 0x15;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set sfd id app config failed");

    request.payload[5] = 0x16;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set psdu data rate app config failed");

    request.payload[5] = 0x17;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set preamble duration app config failed");

    request.payload[5] = 0x18;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set link layer mode app config failed");

    request.payload[5] = 0x19;
    request.payload[6] = 1;
    request.payload[7] = 0x04;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set data repetition count app config failed");

    request.payload[5] = 0x1A;
    request.payload[6] = 1;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ranging time struct app config failed");

    request.payload[5] = 0x1B;
    request.payload[6] = 1;
    request.payload[7] = 0x06;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set slots per rr app config failed");

    request.payload[5] = 0x1C;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set tx adaptive payload power app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x1D;
    request.payload[6] = 2;
    request.payload[7] = 0x2D;
    request.payload[8] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rng data ntf aoa bound app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x1E;
    request.payload[6] = 1;
    request.payload[7] = 0x07;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set responder slot index app config failed");

    request.payload[5] = 0x1F;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set prf mode app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x20;
    request.payload[6] = 2;
    request.payload[7] = 0x00;
    request.payload[8] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set cap size range app config failed");

    request.payload[5] = 0x21;
    request.payload[6] = 2;
    request.payload[7] = 0x10;
    request.payload[8] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set tx jitter window size app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x22;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set scheduled mode app config failed");

    request.payload[5] = 0x23;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set key rotation app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x24;
    request.payload[6] = 2;
    request.payload[7] = 0x20;
    request.payload[8] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set key rotation rate app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x25;
    request.payload[6] = 1;
    request.payload[7] = 0x4B;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set session priority app config failed");

    request.payload[5] = 0x26;
    request.payload[6] = 1;
    request.payload[7] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set mac address mode app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x27;
    request.payload[6] = 2;
    request.payload[7] = 0x34;
    request.payload[8] = 0x12;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set vendor id app config failed");

    request.payload_len = 15;
    request.payload[5] = 0x28;
    request.payload[6] = 8;
    request.payload[7] = 0x01;
    request.payload[8] = 0x02;
    request.payload[9] = 0x03;
    request.payload[10] = 0x04;
    request.payload[11] = 0x05;
    request.payload[12] = 0x06;
    request.payload[13] = 0x07;
    request.payload[14] = 0x08;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set static sts iv app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x29;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set number of sts segments app config failed");

    request.payload[5] = 0x2A;
    request.payload[6] = 1;
    request.payload[7] = 0x04;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set max rr retry app config failed");

    request.payload_len = 11;
    request.payload[5] = 0x2B;
    request.payload[6] = 4;
    request.payload[7] = 0xE8;
    request.payload[8] = 0x03;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set uwb initiation time app config failed");

    request.payload[5] = 0x2C;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set hopping mode app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x2D;
    request.payload[6] = 1;
    request.payload[7] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set block stride length app config failed");

    request.payload[5] = 0x2E;
    request.payload[6] = 1;
    request.payload[7] = 0x07;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set result report config app config failed");

    request.payload[5] = 0x2F;
    request.payload[6] = 1;
    request.payload[7] = 0x04;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set in-band termination attempt count app config failed");

    request.payload_len = 11;
    request.payload[5] = 0x30;
    request.payload[6] = 4;
    request.payload[7] = 0x78;
    request.payload[8] = 0x56;
    request.payload[9] = 0x34;
    request.payload[10] = 0x12;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set sub session id app config failed");

    request.payload[5] = 0x31;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set bprf phr data rate app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x32;
    request.payload[6] = 2;
    request.payload[7] = 0x10;
    request.payload[8] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set max number of measurements app config failed");

    request.payload_len = 11;
    request.payload[5] = 0x33;
    request.payload[6] = 4;
    request.payload[7] = 0x64;
    request.payload[8] = 0x00;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ul tdoa tx interval app config failed");

    request.payload[5] = 0x34;
    request.payload[6] = 4;
    request.payload[7] = 0xFA;
    request.payload[8] = 0x00;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ul tdoa random window app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x35;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set sts length app config failed");

    request.payload[5] = 0x36;
    request.payload[6] = 1;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set suspend ranging rounds app config failed");

    request.payload_len = 10;
    request.payload[5] = 0x37;
    request.payload[6] = 3;
    request.payload[7] = 0x01;
    request.payload[8] = 0x02;
    request.payload[9] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ul tdoa ntf report config app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x38;
    request.payload[6] = 1;
    request.payload[7] = 0x07;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ul tdoa device id app config failed");

    request.payload[5] = 0x39;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ul tdoa tx timestamp app config failed");

    request.payload[5] = 0x40;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa anchor cfo app config failed");

    request.payload[5] = 0x41;
    request.payload[6] = 1;
    request.payload[7] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa anchor location app config failed");

    request.payload[5] = 0x42;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa tx active ranging rounds app config failed");

    request.payload[5] = 0x43;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa block striding app config failed");

    request.payload[5] = 0x44;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa time reference anchor app config failed");

    request.payload_len = 23;
    request.payload[5] = 0x45;
    request.payload[6] = 16;
    memcpy(&request.payload[7], (const uint8_t[]){
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    }, 16);
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set session key app config failed");

    request.payload[5] = 0x46;
    request.payload[6] = 16;
    memcpy(&request.payload[7], (const uint8_t[]){
        0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
    }, 16);
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set subsession key app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x47;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set session data transfer status ntf config failed");

    request.payload_len = 16;
    request.payload[5] = 0x48;
    request.payload[6] = 9;
    memcpy(&request.payload[7], (const uint8_t[]){ 0x01, 0x78, 0x56, 0x34, 0x12, 0x08, 0x07, 0x06, 0x05 }, 9);
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set session time base app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x49;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa responder tof app config failed");

    request.payload[5] = 0x4A;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set secure ranging nefa level app config failed");

    request.payload[5] = 0x4B;
    request.payload[6] = 1;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set secure ranging csw length app config failed");

    request.payload[5] = 0x4C;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set application data endpoint app config failed");

    request.payload[5] = 0x4D;
    request.payload[6] = 1;
    request.payload[7] = 0x0A;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set owr aoa measurement ntf period app config failed");

    request.payload[5] = 0x3A;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set min frames per rr app config failed");

    request.payload_len = 9;
    request.payload[5] = 0x3B;
    request.payload[6] = 2;
    request.payload[7] = 0x00;
    request.payload[8] = 0x04;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set mtu size app config failed");

    request.payload_len = 8;
    request.payload[5] = 0x3C;
    request.payload[6] = 1;
    request.payload[7] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set inter frame interval app config failed");

    request.payload[5] = 0x3D;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa ranging method app config failed");

    request.payload[5] = 0x3E;
    request.payload[6] = 1;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa tx timestamp conf app config failed");

    request.payload[5] = 0x3F;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set dl tdoa hop count app config failed");

    request.payload_len = 8;
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_GET_APP_CONFIG;
    request.payload_len = 6;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 1;
    request.payload[5] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get app config failed");
    ASSERT_TRUE(result.has_response, "get app config response missing");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get app config status");
    ASSERT_EQ_U8(1, result.response.payload[1], "get app config count");
    ASSERT_EQ_U8(0x00, result.response.payload[2], "get app config id");
    ASSERT_EQ_U8(1, result.response.payload[3], "get app config len");
    ASSERT_EQ_U8(0x01, result.response.payload[4], "get app config value");

    request.payload_len = 7;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 2;
    request.payload[5] = 0x00;
    request.payload[6] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get multi app config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get multi app config status");
    ASSERT_EQ_U8(2, result.response.payload[1], "get multi app config count");
    ASSERT_EQ_U8(0x00, result.response.payload[2], "get multi app config first id");
    ASSERT_EQ_U8(0x01, result.response.payload[4], "get multi app config first value");
    ASSERT_EQ_U8(0x03, result.response.payload[5], "get multi app config second id");
    ASSERT_EQ_U8(0x02, result.response.payload[7], "get multi app config second value");

    request.payload_len = 79;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 74;
    request.payload[5] = 0x01;
    request.payload[6] = 0x02;
    request.payload[7] = 0x04;
    request.payload[8] = 0x05;
    request.payload[9] = 0x06;
    request.payload[10] = 0x08;
    request.payload[11] = 0x09;
    request.payload[12] = 0x0A;
    request.payload[13] = 0x0B;
    request.payload[14] = 0x0C;
    request.payload[15] = 0x0D;
    request.payload[16] = 0x0E;
    request.payload[17] = 0x0F;
    request.payload[18] = 0x10;
    request.payload[19] = 0x12;
    request.payload[20] = 0x13;
    request.payload[21] = 0x14;
    request.payload[22] = 0x15;
    request.payload[23] = 0x16;
    request.payload[24] = 0x17;
    request.payload[25] = 0x18;
    request.payload[26] = 0x19;
    request.payload[27] = 0x1A;
    request.payload[28] = 0x1B;
    request.payload[29] = 0x1C;
    request.payload[30] = 0x1D;
    request.payload[31] = 0x1E;
    request.payload[32] = 0x1F;
    request.payload[33] = 0x20;
    request.payload[34] = 0x21;
    request.payload[35] = 0x22;
    request.payload[36] = 0x23;
    request.payload[37] = 0x24;
    request.payload[38] = 0x25;
    request.payload[39] = 0x26;
    request.payload[40] = 0x27;
    request.payload[41] = 0x28;
    request.payload[42] = 0x29;
    request.payload[43] = 0x2A;
    request.payload[44] = 0x2B;
    request.payload[45] = 0x2C;
    request.payload[46] = 0x2D;
    request.payload[47] = 0x2E;
    request.payload[48] = 0x2F;
    request.payload[49] = 0x30;
    request.payload[50] = 0x31;
    request.payload[51] = 0x32;
    request.payload[52] = 0x33;
    request.payload[53] = 0x34;
    request.payload[54] = 0x35;
    request.payload[55] = 0x36;
    request.payload[56] = 0x37;
    request.payload[57] = 0x38;
    request.payload[58] = 0x39;
    request.payload[59] = 0x40;
    request.payload[60] = 0x41;
    request.payload[61] = 0x42;
    request.payload[62] = 0x43;
    request.payload[63] = 0x44;
    request.payload[64] = 0x45;
    request.payload[65] = 0x46;
    request.payload[66] = 0x47;
    request.payload[67] = 0x48;
    request.payload[68] = 0x49;
    request.payload[69] = 0x4A;
    request.payload[70] = 0x4B;
    request.payload[71] = 0x4C;
    request.payload[72] = 0x4D;
    request.payload[73] = 0x3A;
    request.payload[74] = 0x3B;
    request.payload[75] = 0x3C;
    request.payload[76] = 0x3D;
    request.payload[77] = 0x3E;
    request.payload[78] = 0x3F;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get extended app config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get extended app config status");
    ASSERT_EQ_U8(74, result.response.payload[1], "get extended app config count");
    ASSERT_EQ_U8(0x01, result.response.payload[2], "get extended app config first id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x0A,
                                             (const uint8_t[]){ 0x05, 0x00, 0x00, 0x00 }, 4),
                "get extended app config missing sts index");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x0F,
                                             (const uint8_t[]){ 0x64, 0x00 }, 2),
                "get extended app config missing proximity near");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x10,
                                             (const uint8_t[]){ 0xF4, 0x01 }, 2),
                "get extended app config missing proximity far");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x13,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing rssi reporting");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x14,
                                             (const uint8_t[]){ 0x0C }, 1),
                "get extended app config missing preamble code index");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x15,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing sfd id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x16,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get extended app config missing psdu data rate");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x17,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get extended app config missing preamble duration");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x18,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing link layer mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x19,
                                             (const uint8_t[]){ 0x04 }, 1),
                "get extended app config missing data repetition count");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1C,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing tx adaptive payload power");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1D,
                                             (const uint8_t[]){ 0x2D, 0x00 }, 2),
                "get extended app config missing rng data ntf aoa bound");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1E,
                                             (const uint8_t[]){ 0x07 }, 1),
                "get extended app config missing responder slot index");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1F,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing prf mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x20,
                                             (const uint8_t[]){ 0x00, 0x02 }, 2),
                "get extended app config missing cap size range");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x21,
                                             (const uint8_t[]){ 0x10, 0x00 }, 2),
                "get extended app config missing tx jitter window size");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x22,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing scheduled mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x23,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing key rotation");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x24,
                                             (const uint8_t[]){ 0x20, 0x00 }, 2),
                "get extended app config missing key rotation rate");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x25,
                                             (const uint8_t[]){ 0x4B }, 1),
                "get extended app config missing session priority");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x27,
                                             (const uint8_t[]){ 0x34, 0x12 }, 2),
                "get extended app config missing vendor id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x28,
                                             (const uint8_t[]){ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 }, 8),
                "get extended app config missing static sts iv");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x29,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get extended app config missing number of sts segments");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x2A,
                                             (const uint8_t[]){ 0x04 }, 1),
                "get extended app config missing max rr retry");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x2B,
                                             (const uint8_t[]){ 0xE8, 0x03, 0x00, 0x00 }, 4),
                "get extended app config missing uwb initiation time");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x2D,
                                             (const uint8_t[]){ 0x05 }, 1),
                "get extended app config missing block stride length");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x30,
                                             (const uint8_t[]){ 0x78, 0x56, 0x34, 0x12 }, 4),
                "get extended app config missing sub session id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x34,
                                             (const uint8_t[]){ 0xFA, 0x00, 0x00, 0x00 }, 4),
                "get extended app config missing ul tdoa random window");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x35,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get extended app config missing sts length");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x36,
                                             (const uint8_t[]){ 0x03 }, 1),
                "get extended app config missing suspend ranging rounds");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x37,
                                             (const uint8_t[]){ 0x01, 0x02, 0x03 }, 3),
                "get extended app config missing ul tdoa ntf report config");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x38,
                                             (const uint8_t[]){ 0x07 }, 1),
                "get extended app config missing ul tdoa device id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x39,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing ul tdoa tx timestamp");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x40,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing dl tdoa anchor cfo");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x41,
                                             (const uint8_t[]){ 0x00 }, 1),
                "get extended app config missing dl tdoa anchor location");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x42,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing dl tdoa tx active ranging rounds");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x43,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing dl tdoa block striding");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x44,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing dl tdoa time reference anchor");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x45,
                                             (const uint8_t[]){ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                                0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }, 16),
                "get extended app config missing session key");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x46,
                                             (const uint8_t[]){ 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
                                                                0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 }, 16),
                "get extended app config missing subsession key");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x47,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing session data transfer status ntf config");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x48,
                                             (const uint8_t[]){ 0x01, 0x78, 0x56, 0x34, 0x12, 0x08, 0x07, 0x06, 0x05 }, 9),
                "get extended app config missing session time base");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x49,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing dl tdoa responder tof");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4A,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get extended app config missing secure ranging nefa level");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4B,
                                             (const uint8_t[]){ 0x03 }, 1),
                "get extended app config missing secure ranging csw length");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4C,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing application data endpoint");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4D,
                                             (const uint8_t[]){ 0x0A }, 1),
                "get extended app config missing owr aoa measurement ntf period");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x3F,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get extended app config missing dl tdoa hop count");
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get all app config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get all app config status");
    ASSERT_EQ_U8(78, result.response.payload[1], "get all app config count");
    ASSERT_EQ_U8(0x00, result.response.payload[2], "get all app config first id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x0A,
                                             (const uint8_t[]){ 0x05, 0x00, 0x00, 0x00 }, 4),
                "get all app config missing sts index");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x0F,
                                             (const uint8_t[]){ 0x64, 0x00 }, 2),
                "get all app config missing proximity near");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x10,
                                             (const uint8_t[]){ 0xF4, 0x01 }, 2),
                "get all app config missing proximity far");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x13,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing rssi reporting");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x14,
                                             (const uint8_t[]){ 0x0C }, 1),
                "get all app config missing preamble code index");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x15,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing sfd id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x16,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get all app config missing psdu data rate");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x17,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get all app config missing preamble duration");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x18,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing link layer mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x19,
                                             (const uint8_t[]){ 0x04 }, 1),
                "get all app config missing data repetition count");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1C,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing tx adaptive payload power");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1D,
                                             (const uint8_t[]){ 0x2D, 0x00 }, 2),
                "get all app config missing rng data ntf aoa bound");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1E,
                                             (const uint8_t[]){ 0x07 }, 1),
                "get all app config missing responder slot index");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x1F,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing prf mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x20,
                                             (const uint8_t[]){ 0x00, 0x02 }, 2),
                "get all app config missing cap size range");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x21,
                                             (const uint8_t[]){ 0x10, 0x00 }, 2),
                "get all app config missing tx jitter window size");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x22,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing scheduled mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x23,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing key rotation");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x24,
                                             (const uint8_t[]){ 0x20, 0x00 }, 2),
                "get all app config missing key rotation rate");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x25,
                                             (const uint8_t[]){ 0x4B }, 1),
                "get all app config missing session priority");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x27,
                                             (const uint8_t[]){ 0x34, 0x12 }, 2),
                "get all app config missing vendor id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x28,
                                             (const uint8_t[]){ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 }, 8),
                "get all app config missing static sts iv");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x29,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get all app config missing number of sts segments");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x2A,
                                             (const uint8_t[]){ 0x04 }, 1),
                "get all app config missing max rr retry");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x2B,
                                             (const uint8_t[]){ 0xE8, 0x03, 0x00, 0x00 }, 4),
                "get all app config missing uwb initiation time");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x26,
                                             (const uint8_t[]){ 0x00 }, 1),
                "get all app config missing mac address mode");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x2D,
                                             (const uint8_t[]){ 0x05 }, 1),
                "get all app config missing block stride length");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x30,
                                             (const uint8_t[]){ 0x78, 0x56, 0x34, 0x12 }, 4),
                "get all app config missing sub session id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x34,
                                             (const uint8_t[]){ 0xFA, 0x00, 0x00, 0x00 }, 4),
                "get all app config missing ul tdoa random window");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x35,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get all app config missing sts length");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x36,
                                             (const uint8_t[]){ 0x03 }, 1),
                "get all app config missing suspend ranging rounds");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x37,
                                             (const uint8_t[]){ 0x01, 0x02, 0x03 }, 3),
                "get all app config missing ul tdoa ntf report config");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x38,
                                             (const uint8_t[]){ 0x07 }, 1),
                "get all app config missing ul tdoa device id");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x39,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing ul tdoa tx timestamp");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x40,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing dl tdoa anchor cfo");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x41,
                                             (const uint8_t[]){ 0x00 }, 1),
                "get all app config missing dl tdoa anchor location");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x42,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing dl tdoa tx active ranging rounds");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x43,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing dl tdoa block striding");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x44,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing dl tdoa time reference anchor");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x45,
                                             (const uint8_t[]){ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                                0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }, 16),
                "get all app config missing session key");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x46,
                                             (const uint8_t[]){ 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
                                                                0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 }, 16),
                "get all app config missing subsession key");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x47,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing session data transfer status ntf config");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x48,
                                             (const uint8_t[]){ 0x01, 0x78, 0x56, 0x34, 0x12, 0x08, 0x07, 0x06, 0x05 }, 9),
                "get all app config missing session time base");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x49,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing dl tdoa responder tof");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4A,
                                             (const uint8_t[]){ 0x02 }, 1),
                "get all app config missing secure ranging nefa level");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4B,
                                             (const uint8_t[]){ 0x03 }, 1),
                "get all app config missing secure ranging csw length");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4C,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing application data endpoint");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x4D,
                                             (const uint8_t[]){ 0x0A }, 1),
                "get all app config missing owr aoa measurement ntf period");
    ASSERT_TRUE(response_contains_config_tlv(&result.response, 0x3F,
                                             (const uint8_t[]){ 0x01 }, 1),
                "get all app config missing dl tdoa hop count");
    PASS();
}

static void test_profile_rejects_unsupported_session_features(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "profile rejection init failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_SET_APP_CONFIG;
    request.payload_len = 8;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 1;
    request.payload[5] = 0x99;
    request.payload[6] = 1;
    request.payload[7] = 0x01;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "unsupported app config set should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "unsupported app config set status");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = 0x02;
    request.payload_len = 4;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "unsupported session control command should fail");
    ASSERT_EQ_U8(UCI_STATUS_UNKNOWN_OID, result.response.payload[0], "unsupported session control status");
    PASS();
}

static void test_session_lifecycle(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    const uint32_t session_id = 0x12345678U;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session init failed");
    ASSERT_TRUE(result.has_response, "session init response missing");
    ASSERT_TRUE(result.has_notification, "session init notification missing");
    ASSERT_EQ_U32(session_id, read_u32_le(&result.response.payload[1]), "session init id");
    ASSERT_EQ_U8(UCI_SESSION_STATE_INIT, result.notification.payload[4], "session init state ntf");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session start failed");
    ASSERT_EQ_U8(UCI_SESSION_STATE_ACTIVE, result.notification.payload[4], "session active ntf");

    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_GET_STATE;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get state failed");
    ASSERT_EQ_U8(UCI_SESSION_STATE_ACTIVE, result.response.payload[1], "get state active");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_STOP;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "session stop failed");
    ASSERT_EQ_U8(UCI_SESSION_STATE_IDLE, result.notification.payload[4], "session idle ntf");
    PASS();
}

static void test_profile_enforces_session_transition_policy(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_STOP;
    request.payload_len = 4;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "stop without session should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "stop without session status");

    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "init before transition test failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_STOP;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "stop from init should be rejected");
    ASSERT_EQ_U8(UCI_STATUS_REJECTED, result.response.payload[0], "stop from init status");

    request.oid = UCI_SESSION_START;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "start from init should succeed");
    ASSERT_EQ_U8(UCI_SESSION_STATE_ACTIVE, result.notification.payload[4], "start transition notification state");

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "start from active should be rejected");
    ASSERT_EQ_U8(UCI_STATUS_REJECTED, result.response.payload[0], "start from active status");
    PASS();
}

static void test_session_multicast_list_updates(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "multicast init failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_UPDATE_CONTROLLER_MULTICAST_LIST;
    request.payload_len = 12;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 1;
    request.payload[5] = UCI_MULTICAST_ACTION_ADD_SHORT_KEY;
    request.payload[6] = 0x34;
    request.payload[7] = 0x12;
    request.payload[8] = 0xDD;
    request.payload[9] = 0xCC;
    request.payload[10] = 0xBB;
    request.payload[11] = 0xAA;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "multicast add failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "multicast add overall");
    ASSERT_EQ_U8(1, result.response.payload[1], "multicast add processed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[8], "multicast add entry status");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "multicast session lookup");
    ASSERT_TRUE(session->multicast_entries[0].in_use, "multicast entry should exist");

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "multicast duplicate add should fail");
    ASSERT_EQ_U8(UCI_STATUS_FAILED, result.response.payload[0], "multicast duplicate overall");
    ASSERT_EQ_U8(UCI_STATUS_ADDRESS_ALREADY_PRESENT, result.response.payload[8], "multicast duplicate entry");

    request.payload[5] = UCI_MULTICAST_ACTION_REMOVE;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "multicast remove failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "multicast remove overall");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[8], "multicast remove entry");
    ASSERT_TRUE(!session->multicast_entries[0].in_use, "multicast entry should be removed");

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "multicast missing remove should fail");
    ASSERT_EQ_U8(UCI_STATUS_FAILED, result.response.payload[0], "multicast missing remove overall");
    ASSERT_EQ_U8(UCI_STATUS_ADDRESS_NOT_FOUND, result.response.payload[8], "multicast missing remove entry");

    request.payload[5] = 0xFF;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "multicast invalid action should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "multicast invalid action status");

    request.payload[5] = UCI_MULTICAST_ACTION_ADD_SHORT_KEY;
    request.payload_len = 11;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "multicast truncated entry should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "multicast truncated entry status");
    PASS();
}

static void test_session_data_transfer_phase_config(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;

    uci_sim_device_init(&device);
    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dtp init failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_DATA_TRANSFER_PHASE_CONFIG;
    request.payload_len = 10;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 7;
    request.payload[5] = 0xA5;
    request.payload[6] = 3;
    request.payload[7] = 0x11;
    request.payload[8] = 0x22;
    request.payload[9] = 0x33;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dtp config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "dtp status");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "dtp session lookup");
    ASSERT_EQ_U8(7, session->dtp_repetition, "dtp repetition");
    ASSERT_EQ_U8(0xA5, session->dtp_control, "dtp control");
    ASSERT_EQ_U8(3, session->dtp_size, "dtp size");
    ASSERT_EQ_U8(3, session->dtp_payload_len, "dtp payload len");
    ASSERT_EQ_U8(0x11, session->dtp_payload[0], "dtp payload 0");
    ASSERT_EQ_U8(0x22, session->dtp_payload[1], "dtp payload 1");
    ASSERT_EQ_U8(0x33, session->dtp_payload[2], "dtp payload 2");

    request.payload_len = 9;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "dtp wrong size should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_MSG_SIZE, result.response.payload[0], "dtp wrong size status");

    request.payload_len = 10;
    request.payload[0] = 0x11;
    request.payload[1] = 0x22;
    request.payload[2] = 0x33;
    request.payload[3] = 0x44;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "dtp missing session should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "dtp missing session status");
    PASS();
}

static void test_data_message_send_emits_credit_and_status_notifications(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "data send init failed");

    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "data send start failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_DATA;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_DATA_PACKET_FORMAT_SEND;
    request.oid = 0x00;
    request.payload_len = 18;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 0x44;
    request.payload[5] = 0x33;
    request.payload[6] = 0x22;
    request.payload[7] = 0x11;
    request.payload[8] = 0x00;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    request.payload[11] = 0x00;
    request.payload[12] = 0x0F;
    request.payload[13] = 0x00;
    request.payload[14] = 0x02;
    request.payload[15] = 0x00;
    request.payload[16] = 0xAA;
    request.payload[17] = 0xBB;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "data send failed");
    ASSERT_TRUE(result.has_notification, "data send credit notification missing");
    ASSERT_EQ_U8(UCI_SESSION_DATA_CREDIT_NTF, result.notification.oid, "data send credit oid");
    ASSERT_EQ_U8(1, result.notification.payload[4], "data send credit available");
    ASSERT_TRUE(device.pending_notification_count > 0, "data send status notification queued");
    ASSERT_EQ_U8(UCI_SESSION_DATA_TRANSFER_STATUS_NTF, device.pending_notifications[0].oid, "data send status oid");
    ASSERT_EQ_U8(UCI_DATA_TRANSFER_STATUS_OK, device.pending_notifications[0].payload[6], "data send status value");
    PASS();
}

static void test_data_message_send_edge_cases(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "data edge init failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_DATA;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_DATA_PACKET_FORMAT_SEND;
    request.oid = 0x00;
    request.payload_len = 18;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 0x44;
    request.payload[5] = 0x33;
    request.payload[6] = 0x22;
    request.payload[7] = 0x11;
    request.payload[8] = 0x00;
    request.payload[9] = 0x00;
    request.payload[10] = 0x00;
    request.payload[11] = 0x00;
    request.payload[12] = 0x0F;
    request.payload[13] = 0x00;
    request.payload[14] = 0x02;
    request.payload[15] = 0x00;
    request.payload[16] = 0xAA;
    request.payload[17] = 0xBB;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "inactive data send should fail");
    ASSERT_TRUE(result.has_notification, "inactive data send status missing");
    ASSERT_EQ_U8(UCI_SESSION_DATA_TRANSFER_STATUS_NTF, result.notification.oid, "inactive data send oid");
    ASSERT_EQ_U8(UCI_DATA_TRANSFER_STATUS_ERROR_REJECTED, result.notification.payload[6], "inactive data send status");

    request.payload[14] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "invalid-format data send should fail");
    ASSERT_TRUE(result.has_notification, "invalid-format data send status missing");
    ASSERT_EQ_U8(UCI_DATA_TRANSFER_STATUS_INVALID_FORMAT, result.notification.payload[6], "invalid-format data send status");

    request.payload[14] = 0x02;
    request.mt = UCI_MT_COMMAND;
    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_START;
    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "data edge start failed");

    request.mt = UCI_MT_DATA;
    request.gid = UCI_DATA_PACKET_FORMAT_SEND;
    request.oid = 0x00;
    request.payload_len = 18;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "first repeated-send baseline failed");
    ASSERT_EQ_U8(UCI_DATA_TRANSFER_STATUS_OK, device.pending_notifications[0].payload[6], "first repeated-send status");
    device.pending_notification_count = 0;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "duplicate repeated-send failed");
    ASSERT_EQ_U8(UCI_DATA_TRANSFER_STATUS_REPETITION_OK, device.pending_notifications[0].payload[6], "duplicate repeated-send status");
    PASS();
}

static void test_logical_link_lifecycle(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "logical link init failed");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_LOGICAL_LINK_CREATE;
    request.payload_len = 4;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "logical link short create should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_MSG_SIZE, result.response.payload[0], "logical link short create status");

    request.payload_len = 7;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 0x12;
    request.payload[5] = 0x77;
    request.payload[6] = 0x05;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "logical link create failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "logical link create status");
    ASSERT_EQ_U8(0x12, result.response.payload[1], "logical link create id");
    ASSERT_EQ_U8(0x05, result.response.payload[2], "logical link create credit");
    ASSERT_TRUE(result.has_notification, "logical link create ntf missing");
    ASSERT_EQ_U8(UCI_SESSION_LOGICAL_LINK_UWBS_CREATE, result.notification.oid, "logical link create ntf oid");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "logical link session lookup");
    ASSERT_EQ_U8(1, session->logical_link_count, "logical link count after create");

    request.oid = UCI_SESSION_LOGICAL_LINK_GET_PARAM;
    request.payload_len = 5;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "logical link get param failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "logical link get param status");
    ASSERT_EQ_U8(0x12, result.response.payload[1], "logical link get param id");
    ASSERT_EQ_U8(0x77, result.response.payload[2], "logical link get param mode");
    ASSERT_EQ_U8(0x05, result.response.payload[3], "logical link get param credit");

    request.oid = UCI_SESSION_LOGICAL_LINK_CLOSE;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "logical link close failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "logical link close status");
    ASSERT_TRUE(result.has_notification, "logical link close ntf missing");
    ASSERT_EQ_U8(UCI_SESSION_LOGICAL_LINK_UWBS_CLOSE, result.notification.oid, "logical link close ntf oid");
    ASSERT_EQ_U8(0, session->logical_link_count, "logical link count after close");
    PASS();
}

static void test_logical_link_edge_cases(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;
    uint8_t i;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "logical link edge init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "logical link edge session lookup");

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONTROL;
    request.oid = UCI_SESSION_LOGICAL_LINK_CREATE;
    request.payload_len = 7;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 0x12;
    request.payload[5] = 0x77;
    request.payload[6] = 0x05;

    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "logical link edge initial create failed");
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "logical link duplicate create should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "logical link duplicate status");

    request.oid = UCI_SESSION_LOGICAL_LINK_GET_PARAM;
    request.payload_len = 5;
    request.payload[4] = 0x7E;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "logical link missing get param should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "logical link missing get param status");

    request.oid = UCI_SESSION_LOGICAL_LINK_CLOSE;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "logical link missing close should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "logical link missing close status");

    request.payload_len = 4;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "logical link short close should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_MSG_SIZE, result.response.payload[0], "logical link short close status");
    request.payload_len = 5;

    memset(session->logical_links, 0, sizeof(session->logical_links));
    session->logical_link_count = UCI_SIM_MAX_LOGICAL_LINKS;
    for (i = 0; i < UCI_SIM_MAX_LOGICAL_LINKS; ++i) {
        session->logical_links[i].in_use = 1;
        session->logical_links[i].link_id = i;
    }

    request.oid = UCI_SESSION_LOGICAL_LINK_CREATE;
    request.payload_len = 7;
    request.payload[4] = 0x34;
    request.payload[5] = 0x01;
    request.payload[6] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "logical link full create should fail");
    ASSERT_EQ_U8(UCI_STATUS_MULTICAST_LIST_FULL, result.response.payload[0], "logical link full status");
    PASS();
}

static void test_dt_round_update_commands(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dt rounds init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "dt rounds session lookup");

    request.oid = UCI_SESSION_UPDATE_DT_ANCHOR_RANGING_ROUNDS;
    request.payload_len = 8;
    request.payload[4] = 3;
    request.payload[5] = 0x01;
    request.payload[6] = 0x05;
    request.payload[7] = 0x09;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dt anchor update failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "dt anchor update status");
    ASSERT_EQ_U8(3, result.response.payload[1], "dt anchor stored count");
    ASSERT_EQ_U8(0x01, result.response.payload[2], "dt anchor first round");
    ASSERT_EQ_U8(0x05, result.response.payload[3], "dt anchor second round");
    ASSERT_EQ_U8(0x09, result.response.payload[4], "dt anchor third round");
    ASSERT_EQ_U8(3, session->dt_anchor_round_count, "dt anchor session count");
    ASSERT_EQ_U8(0x01, session->dt_anchor_round_indexes[0], "dt anchor session first round");
    ASSERT_EQ_U8(0x05, session->dt_anchor_round_indexes[1], "dt anchor session second round");
    ASSERT_EQ_U8(0x09, session->dt_anchor_round_indexes[2], "dt anchor session third round");

    request.oid = UCI_SESSION_UPDATE_DT_TAG_RANGING_ROUNDS;
    request.payload_len = 7;
    request.payload[4] = 2;
    request.payload[5] = 0x03;
    request.payload[6] = 0x07;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dt tag update failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "dt tag update status");
    ASSERT_EQ_U8(2, result.response.payload[1], "dt tag stored count");
    ASSERT_EQ_U8(0x03, result.response.payload[2], "dt tag first round");
    ASSERT_EQ_U8(0x07, result.response.payload[3], "dt tag second round");
    ASSERT_EQ_U8(2, session->dt_tag_round_count, "dt tag session count");
    ASSERT_EQ_U8(0x03, session->dt_tag_round_indexes[0], "dt tag session first round");
    ASSERT_EQ_U8(0x07, session->dt_tag_round_indexes[1], "dt tag session second round");

    request.oid = UCI_SESSION_UPDATE_DT_TAG_RANGING_ROUNDS;
    request.payload_len = 5;
    request.payload[4] = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "dt tag clear failed");
    ASSERT_EQ_U8(0, session->dt_tag_round_count, "dt tag clear count");
    ASSERT_EQ_U8(0, result.response.payload[1], "dt tag clear stored count");
    PASS();
}

static void test_hus_config_commands(void) {
    uci_sim_device_t device;
    uci_sim_packet_t request;
    uci_sim_result_t result;
    uci_sim_session_t* session = NULL;

    uci_sim_device_init(&device);

    memset(&request, 0, sizeof(request));
    request.mt = UCI_MT_COMMAND;
    request.pbf = UCI_PBF_COMPLETE;
    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_INIT;
    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = UCI_SESSION_TYPE_RANGING;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "hus init failed");
    ASSERT_TRUE(uci_sim_device_get_session(&device, 0x12345678U, &session) == 0, "hus session lookup");

    request.oid = UCI_SESSION_SET_HUS_CONTROLLER_CONFIG;
    request.payload_len = 15;
    request.payload[4] = 0xEF;
    request.payload[5] = 0xCD;
    request.payload[6] = 0xAB;
    request.payload[7] = 0x90;
    request.payload[8] = 0x00;
    request.payload[9] = 0xAA;
    request.payload[10] = 0x03;
    request.payload[11] = 0x00;
    request.payload[12] = 0x11;
    request.payload[13] = 0x22;
    request.payload[14] = 0x33;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "hus controller config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "hus controller status");
    ASSERT_EQ_U32(0x90ABCDEFU, session->hus_controller_primary_session_id, "hus controller primary session");
    ASSERT_EQ_U8(0x00, session->hus_controller_role, "hus controller role");
    ASSERT_EQ_U8(0xAA, session->hus_controller_reserved, "hus controller reserved");
    ASSERT_EQ_U32(3, session->hus_controller_config_length, "hus controller config length");
    ASSERT_EQ_U8(0x11, session->hus_controller_config_data[0], "hus controller config byte 0");
    ASSERT_EQ_U8(0x22, session->hus_controller_config_data[1], "hus controller config byte 1");
    ASSERT_EQ_U8(0x33, session->hus_controller_config_data[2], "hus controller config byte 2");

    request.oid = UCI_SESSION_SET_HUS_CONTROLEE_CONFIG;
    request.payload_len = 14;
    request.payload[4] = 0x04;
    request.payload[5] = 0x03;
    request.payload[6] = 0x02;
    request.payload[7] = 0x01;
    request.payload[8] = 0x01;
    request.payload[9] = 0x55;
    request.payload[10] = 0x02;
    request.payload[11] = 0x00;
    request.payload[12] = 0x44;
    request.payload[13] = 0x66;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "hus controlee config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "hus controlee status");
    ASSERT_EQ_U32(0x01020304U, session->hus_controlee_primary_session_id, "hus controlee primary session");
    ASSERT_EQ_U8(0x01, session->hus_controlee_role, "hus controlee role");
    ASSERT_EQ_U8(0x55, session->hus_controlee_reserved, "hus controlee reserved");
    ASSERT_EQ_U32(2, session->hus_controlee_config_length, "hus controlee config length");
    ASSERT_EQ_U8(0x44, session->hus_controlee_config_data[0], "hus controlee config byte 0");
    ASSERT_EQ_U8(0x66, session->hus_controlee_config_data[1], "hus controlee config byte 1");

    request.oid = UCI_SESSION_SET_HUS_CONTROLLER_CONFIG;
    request.payload_len = 11;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "hus short payload should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_MSG_SIZE, result.response.payload[0], "hus short payload status");

    request.payload_len = 12;
    request.payload[8] = 0x02;
    request.payload[10] = 0x00;
    request.payload[11] = 0x00;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) != 0, "hus invalid role should fail");
    ASSERT_EQ_U8(UCI_STATUS_INVALID_PARAM, result.response.payload[0], "hus invalid role status");
    PASS();
}

int main(void) {
    test_packet_round_trip();
    test_engine_clock_poll_progression();
    test_engine_uses_session_ranging_interval_override();
    test_ranging_interval_validation_rejects_below_profile_min();
    test_session_start_rejects_invalid_ranging_interval();
    test_result_report_config_validation_rejects_unsupported_bits();
    test_session_start_rejects_invalid_result_report_config();
    test_sts_config_validation_rejects_unsupported_values();
    test_session_start_rejects_static_sts_without_iv();
    test_session_start_rejects_provisioned_sts_without_session_key();
    test_aoa_result_req_validation_rejects_unsupported_values();
    test_session_start_rejects_invalid_aoa_result_req();
    test_prf_mode_validation_rejects_unsupported_values();
    test_session_start_rejects_invalid_prf_mode();
    test_preamble_code_index_validation_rejects_unsupported_values();
    test_session_start_rejects_invalid_preamble_code_index();
    test_rssi_reporting_validation_rejects_unsupported_values();
    test_session_start_rejects_invalid_rssi_reporting();
    test_ranging_round_usage_validation_rejects_unsupported_values();
    test_device_type_validation_rejects_unsupported_values();
    test_multi_node_mode_validation_rejects_unsupported_values();
    test_channel_number_validation_rejects_unsupported_values();
    test_session_start_rejects_invalid_channel_number();
    test_number_of_controlees_validation_rejects_excessive_value();
    test_mac_address_mode_validation_rejects_unsupported_value();
    test_device_mac_address_validation_rejects_invalid_length();
    test_dst_mac_address_validation_rejects_invalid_list_length();
    test_session_start_rejects_device_type_role_mismatch();
    test_session_start_rejects_unicast_multi_node_topology_mismatch();
    test_session_start_rejects_controlee_count_dst_list_mismatch();
    test_session_start_rejects_invalid_ranging_round_usage();
    test_measurement_policy_serializes_default_range_notification();
    test_measurement_policy_serializes_session_ranging_interval_override();
    test_result_report_config_masks_range_notification_fields();
    test_aoa_result_req_masks_range_notification_axes();
    test_rssi_reporting_masks_range_notification_rssi();
    test_core_device_info();
    test_default_profile_is_applied();
    test_default_profile_feature_matrix();
    test_core_device_config_storage();
    test_core_additional_device_configs();
    test_profile_rejects_unsupported_core_features();
    test_core_caps_match_profile();
    test_core_query_timestamp_response();
    test_core_device_reset_restores_profile_defaults();
    test_default_scenario_initialization();
    test_delayed_notification_scenario();
    test_session_get_count();
    test_session_query_data_size_and_ranging_count();
    test_ranging_stream_scenario();
    test_ranging_stream_respects_info_ntf_disable();
    test_ranging_stream_respects_proximity_inside_mode();
    test_ranging_stream_respects_proximity_transition_mode();
    test_ranging_stream_progresses_to_completion();
    test_session_app_config_storage();
    test_profile_rejects_unsupported_session_features();
    test_session_lifecycle();
    test_profile_enforces_session_transition_policy();
    test_session_multicast_list_updates();
    test_session_data_transfer_phase_config();
    test_data_message_send_emits_credit_and_status_notifications();
    test_data_message_send_edge_cases();
    test_logical_link_lifecycle();
    test_logical_link_edge_cases();
    test_dt_round_update_commands();
    test_hus_config_commands();

    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
