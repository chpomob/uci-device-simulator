#include "uci_sim_device.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;
static int g_passed = 0;

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
    test_core_device_info();
    test_session_lifecycle();

    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
