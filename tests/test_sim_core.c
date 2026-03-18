#include "uci_sim_device.h"
#include "uci_sim_engine.h"
#include "uci_sim_profile.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;
static int g_passed = 0;
static uci_sim_time_ms_t g_fake_clock_ms = 0;

#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_U8(exp, act, msg) do { if ((unsigned)(exp) != (unsigned)(act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_U32(exp, act, msg) do { if ((unsigned long)(exp) != (unsigned long)(act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define PASS() do { g_passed++; } while (0)

static uint32_t read_u32_le(const uint8_t* payload) {
    return (uint32_t)payload[0] |
           ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) |
           ((uint32_t)payload[3] << 24);
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
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock range 1 missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine clock range 2 should wait for next poll");

    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine zero-delta poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock range 2 missing");
    ASSERT_EQ_U32(2, read_u32_le(packet.payload), "engine clock range 2 sequence");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine clock range 3 should not be immediate");

    g_fake_clock_ms = 1000;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine pre-deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) != 0, "engine clock range 3 should wait for deadline");

    g_fake_clock_ms = 1001;
    ASSERT_TRUE(uci_sim_engine_poll(&engine) == 0, "engine deadline poll failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &packet) == 0, "engine clock range 3 missing");
    ASSERT_EQ_U32(3, read_u32_le(packet.payload), "engine clock range 3 sequence");
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
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0B),
                "profile should support app config 0x0B");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0C),
                "profile should support app config 0x0C");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0D),
                "profile should support app config 0x0D");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x0E),
                "profile should support app config 0x0E");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x12),
                "profile should support app config 0x12");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x1A),
                "profile should support app config 0x1A");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x1B),
                "profile should support app config 0x1B");
    ASSERT_TRUE(uci_sim_profile_supports_session_app_config(profile, 0x2E),
                "profile should support app config 0x2E");
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
    ASSERT_TRUE(dequeue_outbound(&engine, &queued) == 0, "ranging stream pending dequeue failed");
    ASSERT_EQ_U8(UCI_GID_SESSION_CONTROL, queued.gid, "ranging stream data gid");
    ASSERT_EQ_U8(profile->range_data_notification_oid, queued.oid, "ranging stream data oid");
    ASSERT_EQ_U8(profile->range_data_payload_len, (uint8_t)queued.payload_len, "ranging stream payload len");
    ASSERT_EQ_U8(profile->range_data_payload_template[24], queued.payload[24], "ranging stream measurement count");
    sequence = read_u32_le(queued.payload);
    ASSERT_EQ_U32(1, sequence, "ranging stream sequence 1");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.pending_notification_count, "ranging stream pending drained after start");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.sessions[0].ranging_count, "ranging stream count after start");
    ASSERT_EQ_U8(profile->ranging_stream_burst_count - 1,
                 engine.device.sessions[0].ranging_stream_remaining,
                 "ranging stream remaining after start");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after start");
    ASSERT_EQ_U8(profile->range_data_payload_template[25], queued.payload[25], "ranging stream short addr lo");
    ASSERT_EQ_U8(profile->range_data_payload_template[26], queued.payload[26], "ranging stream short addr hi");
    ASSERT_EQ_U8((uint8_t)(profile->range_data_distance_base_cm & 0xFFU),
                 queued.payload[profile->range_data_measurement_distance_offset],
                 "ranging stream distance lo");

    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_GET_STATE;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging stream get state failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging stream get state response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging stream follow-up notification missing");
    ASSERT_EQ_U8(UCI_GID_SESSION_CONTROL, notification.gid, "ranging stream follow-up gid");
    ASSERT_EQ_U8(UCI_SESSION_START, notification.oid, "ranging stream follow-up oid");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(2, sequence, "ranging stream sequence 2");
    ASSERT_EQ_U8(2, (uint8_t)engine.device.sessions[0].ranging_count, "ranging stream count after follow-up");
    ASSERT_EQ_U8(1, engine.device.sessions[0].ranging_stream_remaining, "ranging stream remaining after follow-up");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after follow-up");

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
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression dequeue 1 failed");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(1, sequence, "ranging progression sequence 1");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging progression scheduled after first");

    request.gid = UCI_GID_SESSION_CONFIG;
    request.oid = UCI_SESSION_GET_STATE;
    ASSERT_TRUE(uci_sim_engine_submit_packet(&engine, &request) == 0, "ranging progression get state 1 failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &response) == 0, "ranging progression get state 1 response missing");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression get state 1 notification missing");
    sequence = read_u32_le(notification.payload);
    ASSERT_EQ_U32(2, sequence, "ranging progression sequence 2");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging progression scheduled after second");

    ASSERT_TRUE(uci_sim_engine_tick(&engine, 1000) == 0, "ranging progression async tick failed");
    ASSERT_TRUE(dequeue_outbound(&engine, &notification) == 0, "ranging progression get state 2 notification missing");
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

    request.payload[5] = 0x12;
    request.payload[6] = 1;
    request.payload[7] = 0x02;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set rframe config app config failed");

    request.payload[5] = 0x1A;
    request.payload[6] = 1;
    request.payload[7] = 0x03;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set ranging time struct app config failed");

    request.payload[5] = 0x1B;
    request.payload[6] = 1;
    request.payload[7] = 0x06;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set slots per rr app config failed");

    request.payload[5] = 0x2E;
    request.payload[6] = 1;
    request.payload[7] = 0x07;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "set result report config app config failed");

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

    request.payload_len = 20;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 15;
    request.payload[5] = 0x01;
    request.payload[6] = 0x02;
    request.payload[7] = 0x04;
    request.payload[8] = 0x05;
    request.payload[9] = 0x06;
    request.payload[10] = 0x08;
    request.payload[11] = 0x09;
    request.payload[12] = 0x0B;
    request.payload[13] = 0x0C;
    request.payload[14] = 0x0D;
    request.payload[15] = 0x0E;
    request.payload[16] = 0x12;
    request.payload[17] = 0x1A;
    request.payload[18] = 0x1B;
    request.payload[19] = 0x2E;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get extended app config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get extended app config status");
    ASSERT_EQ_U8(15, result.response.payload[1], "get extended app config count");
    ASSERT_EQ_U8(0x01, result.response.payload[2], "get extended app config first id");
    ASSERT_EQ_U8(0x01, result.response.payload[4], "get extended app config first value");
    ASSERT_EQ_U8(0x02, result.response.payload[5], "get extended app config second id");
    ASSERT_EQ_U8(0x01, result.response.payload[7], "get extended app config second value");
    ASSERT_EQ_U8(0x04, result.response.payload[8], "get extended app config third id");
    ASSERT_EQ_U8(0x05, result.response.payload[10], "get extended app config third value");
    ASSERT_EQ_U8(0x05, result.response.payload[11], "get extended app config fourth id");
    ASSERT_EQ_U8(0x03, result.response.payload[13], "get extended app config fourth value");
    ASSERT_EQ_U8(0x06, result.response.payload[14], "get extended app config fifth id");
    ASSERT_EQ_U8(0xCD, result.response.payload[16], "get extended app config fifth value 0");
    ASSERT_EQ_U8(0xAB, result.response.payload[17], "get extended app config fifth value 1");
    ASSERT_EQ_U8(0x08, result.response.payload[18], "get extended app config sixth id");
    ASSERT_EQ_U8(0x60, result.response.payload[20], "get extended app config sixth value 0");
    ASSERT_EQ_U8(0x09, result.response.payload[21], "get extended app config sixth value 1");
    ASSERT_EQ_U8(0x09, result.response.payload[22], "get extended app config seventh id");
    ASSERT_EQ_U8(0xD0, result.response.payload[24], "get extended app config seventh value 0");
    ASSERT_EQ_U8(0x07, result.response.payload[25], "get extended app config seventh value 1");
    ASSERT_EQ_U8(0x00, result.response.payload[26], "get extended app config seventh value 2");
    ASSERT_EQ_U8(0x00, result.response.payload[27], "get extended app config seventh value 3");
    ASSERT_EQ_U8(0x0B, result.response.payload[28], "get extended app config eighth id");
    ASSERT_EQ_U8(0x01, result.response.payload[30], "get extended app config eighth value");
    ASSERT_EQ_U8(0x0C, result.response.payload[31], "get extended app config ninth id");
    ASSERT_EQ_U8(0x05, result.response.payload[33], "get extended app config ninth value");
    ASSERT_EQ_U8(0x0D, result.response.payload[34], "get extended app config tenth id");
    ASSERT_EQ_U8(0x03, result.response.payload[36], "get extended app config tenth value");
    ASSERT_EQ_U8(0x0E, result.response.payload[37], "get extended app config eleventh id");
    ASSERT_EQ_U8(0x02, result.response.payload[39], "get extended app config eleventh value");
    ASSERT_EQ_U8(0x12, result.response.payload[40], "get extended app config twelfth id");
    ASSERT_EQ_U8(0x02, result.response.payload[42], "get extended app config twelfth value");
    ASSERT_EQ_U8(0x1A, result.response.payload[43], "get extended app config thirteenth id");
    ASSERT_EQ_U8(0x03, result.response.payload[45], "get extended app config thirteenth value");
    ASSERT_EQ_U8(0x1B, result.response.payload[46], "get extended app config fourteenth id");
    ASSERT_EQ_U8(0x06, result.response.payload[48], "get extended app config fourteenth value");
    ASSERT_EQ_U8(0x2E, result.response.payload[49], "get extended app config fifteenth id");
    ASSERT_EQ_U8(0x07, result.response.payload[51], "get extended app config fifteenth value");

    request.payload_len = 5;
    request.payload[0] = 0x78;
    request.payload[1] = 0x56;
    request.payload[2] = 0x34;
    request.payload[3] = 0x12;
    request.payload[4] = 0;
    ASSERT_TRUE(uci_sim_device_handle_packet(&device, &request, &result) == 0, "get all app config failed");
    ASSERT_EQ_U8(UCI_STATUS_OK, result.response.payload[0], "get all app config status");
    ASSERT_EQ_U8(19, result.response.payload[1], "get all app config count");
    ASSERT_EQ_U8(0x00, result.response.payload[2], "get all app config first id");
    ASSERT_EQ_U8(0x01, result.response.payload[5], "get all app config second id");
    ASSERT_EQ_U8(0x02, result.response.payload[8], "get all app config third id");
    ASSERT_EQ_U8(0x03, result.response.payload[11], "get all app config fourth id");
    ASSERT_EQ_U8(0x04, result.response.payload[14], "get all app config fifth id");
    ASSERT_EQ_U8(0x05, result.response.payload[17], "get all app config sixth id");
    ASSERT_EQ_U8(0x06, result.response.payload[20], "get all app config seventh id");
    ASSERT_EQ_U8(0x07, result.response.payload[24], "get all app config eighth id");
    ASSERT_EQ_U8(0x08, result.response.payload[28], "get all app config ninth id");
    ASSERT_EQ_U8(0x09, result.response.payload[32], "get all app config tenth id");
    ASSERT_EQ_U8(0x0B, result.response.payload[38], "get all app config eleventh id");
    ASSERT_EQ_U8(0x0C, result.response.payload[41], "get all app config twelfth id");
    ASSERT_EQ_U8(0x0D, result.response.payload[44], "get all app config thirteenth id");
    ASSERT_EQ_U8(0x0E, result.response.payload[47], "get all app config fourteenth id");
    ASSERT_EQ_U8(0x11, result.response.payload[50], "get all app config fifteenth id");
    ASSERT_EQ_U8(0x12, result.response.payload[53], "get all app config sixteenth id");
    ASSERT_EQ_U8(0x1A, result.response.payload[56], "get all app config seventeenth id");
    ASSERT_EQ_U8(0x1B, result.response.payload[59], "get all app config eighteenth id");
    ASSERT_EQ_U8(0x2E, result.response.payload[62], "get all app config nineteenth id");
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
