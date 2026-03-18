#include "uci_sim_device.h"
#include "uci_sim_engine.h"

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
    ASSERT_EQ_U8(UCI_SESSION_START, queued.oid, "ranging stream data oid");
    ASSERT_EQ_U8(52, (uint8_t)queued.payload_len, "ranging stream payload len");
    ASSERT_EQ_U8(1, queued.payload[24], "ranging stream measurement count");
    sequence = read_u32_le(queued.payload);
    ASSERT_EQ_U32(1, sequence, "ranging stream sequence 1");
    ASSERT_EQ_U8(0, (uint8_t)engine.device.pending_notification_count, "ranging stream pending drained after start");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.sessions[0].ranging_count, "ranging stream count after start");
    ASSERT_EQ_U8(2, engine.device.sessions[0].ranging_stream_remaining, "ranging stream remaining after start");
    ASSERT_EQ_U8(1, (uint8_t)engine.device.scheduled_event_count, "ranging stream scheduled after start");

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
    request.payload_len = 8;
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
    ASSERT_EQ_U8(1, result.response.payload[1], "set app config count");

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

int main(void) {
    test_packet_round_trip();
    test_engine_clock_poll_progression();
    test_core_device_info();
    test_core_device_config_storage();
    test_core_additional_device_configs();
    test_default_scenario_initialization();
    test_delayed_notification_scenario();
    test_session_get_count();
    test_session_query_data_size_and_ranging_count();
    test_ranging_stream_scenario();
    test_ranging_stream_progresses_to_completion();
    test_session_app_config_storage();
    test_session_lifecycle();

    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
