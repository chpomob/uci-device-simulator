#define _POSIX_C_SOURCE 200809L

#include "uci_sim_engine.h"
#include "uci_sim_packet.h"
#include "uci_sim_tcp_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_failed = 0;
static int g_passed = 0;

#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_INT(exp, act, msg) do { if ((exp) != (act)) { printf("FAIL: %s (expected=%d, actual=%d)\n", msg, (int)(exp), (int)(act)); g_failed++; return; } } while (0)
#define ASSERT_MEMEQ(exp, act, len, msg) do { if (memcmp((exp), (act), (len)) != 0) { printf("FAIL: %s (len=%zu, first diff at byte ", msg, (size_t)(len)); for(size_t _di=0;_di<(size_t)(len);_di++){if(((const uint8_t*)(exp))[_di]!=((const uint8_t*)(act))[_di]){printf("%zu: exp=%02x act=%02x",_di,((const uint8_t*)(exp))[_di],((const uint8_t*)(act))[_di]);break;}} printf(")\n"); g_failed++; return; } } while (0)
#define PASS() do { g_passed++; } while (0)

typedef struct {
    pid_t pid;
    uint16_t port;
    uci_sim_scenario_kind_t scenario;
} test_server_t;

typedef struct {
    const char* request_fixture;
    const char* response_fixture;
    const char* notification_fixture;
    const char* step_name;
} tcp_interop_step_t;

static uint16_t read_u16_le(const uint8_t* payload) {
    return (uint16_t)payload[0] |
           (uint16_t)((uint16_t)payload[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* payload) {
    return (uint32_t)payload[0] |
           ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) |
           ((uint32_t)payload[3] << 24);
}

static ssize_t read_full(int fd, void* buffer, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t rc = recv(fd, (char*)buffer + total, count - total, 0);
        if (rc <= 0) {
            return rc;
        }
        total += (size_t)rc;
    }
    return (ssize_t)total;
}

static ssize_t write_full(int fd, const void* buffer, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t rc = send(fd, (const char*)buffer + total, count - total, 0);
        if (rc <= 0) {
            return rc;
        }
        total += (size_t)rc;
    }
    return (ssize_t)total;
}

static int read_packet(int fd, uint8_t* buffer, size_t buffer_capacity, size_t* packet_len) {
    uint8_t header[UCI_SIM_HEADER_SIZE];
    size_t payload_len;

    if (!buffer || buffer_capacity < UCI_SIM_HEADER_SIZE || !packet_len) {
        return -1;
    }

    if (read_full(fd, header, sizeof(header)) != (ssize_t)sizeof(header)) {
        return -1;
    }

    payload_len = (((header[0] >> 5) & 0x03) == UCI_MT_DATA)
        ? ((size_t)header[2] | ((size_t)header[3] << 8))
        : (size_t)header[3];
    if (buffer_capacity < UCI_SIM_HEADER_SIZE + payload_len) {
        return -1;
    }

    memcpy(buffer, header, sizeof(header));
    if (payload_len > 0 &&
        read_full(fd, buffer + UCI_SIM_HEADER_SIZE, payload_len) != (ssize_t)payload_len) {
        return -1;
    }

    *packet_len = UCI_SIM_HEADER_SIZE + payload_len;
    return 0;
}

static int connect_with_retry(uint16_t port) {
    struct sockaddr_in addr;
    int fd;
    int attempt;
    struct timespec retry_sleep = {0, 20 * 1000 * 1000};

    for (attempt = 0; attempt < 50; ++attempt) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            /* Set a receive timeout to prevent hangs */
            struct timeval tv = {5, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            return fd;
        }

        close(fd);
        nanosleep(&retry_sleep, NULL);
    }

    return -1;
}

static int set_socket_timeout_ms(int fd, int timeout_ms) {
    struct timeval timeout;

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        return -1;
    }
    return 0;
}

static int assert_fixture_packet(int fd, const char* fixture_path, const char* step_name);

static void set_app_config_single_tlv(int fd,
                                      uint32_t session_id,
                                      uint8_t config_id,
                                      const uint8_t* value,
                                      uint8_t value_len,
                                      const char* step_name) {
    uint8_t request[UCI_SIM_MAX_PACKET] = {0};
    char message[160];

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = (uint8_t)(7U + value_len);
    request[4] = (uint8_t)(session_id & 0xFFU);
    request[5] = (uint8_t)((session_id >> 8) & 0xFFU);
    request[6] = (uint8_t)((session_id >> 16) & 0xFFU);
    request[7] = (uint8_t)((session_id >> 24) & 0xFFU);
    request[8] = 0x01;
    request[9] = config_id;
    request[10] = value_len;
    memcpy(&request[11], value, value_len);

    snprintf(message, sizeof(message), "%s write app config", step_name);
    ASSERT_TRUE(write_full(fd, request, 11 + value_len) == (ssize_t)(11 + value_len), message);
    snprintf(message, sizeof(message), "%s set app config rsp", step_name);
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          message);
}

static void set_ranging_interval_ms_for_session(int fd,
                                                uint32_t session_id,
                                                uint32_t interval_ms,
                                                const char* step_name) {
    uint8_t value[4];

    value[0] = (uint8_t)(interval_ms & 0xFFU);
    value[1] = (uint8_t)((interval_ms >> 8) & 0xFFU);
    value[2] = (uint8_t)((interval_ms >> 16) & 0xFFU);
    value[3] = (uint8_t)((interval_ms >> 24) & 0xFFU);
    set_app_config_single_tlv(fd, session_id, UCI_APP_CONFIG_RANGING_INTERVAL, value, 4, step_name);
}

static void set_ranging_interval_ms(int fd, uint32_t interval_ms, const char* step_name) {
    set_ranging_interval_ms_for_session(fd, 0x12345678U, interval_ms, step_name);
}

static void set_max_number_of_measurements(int fd,
                                           uint16_t max_number_of_measurements,
                                           const char* step_name) {
    uint8_t value[2];

    value[0] = (uint8_t)(max_number_of_measurements & 0xFFU);
    value[1] = (uint8_t)((max_number_of_measurements >> 8) & 0xFFU);
    set_app_config_single_tlv(fd,
                              0x12345678U,
                              UCI_APP_CONFIG_MAX_NUMBER_OF_MEASUREMENTS,
                              value,
                              2,
                              step_name);
}

static void set_session_time_base(int fd,
                                  uint32_t session_id,
                                  uint8_t flags,
                                  uint32_t reference_session_id,
                                  uint32_t offset_us,
                                  const char* step_name) {
    uint8_t value[9];

    value[0] = flags;
    value[1] = (uint8_t)(reference_session_id & 0xFFU);
    value[2] = (uint8_t)((reference_session_id >> 8) & 0xFFU);
    value[3] = (uint8_t)((reference_session_id >> 16) & 0xFFU);
    value[4] = (uint8_t)((reference_session_id >> 24) & 0xFFU);
    value[5] = (uint8_t)(offset_us & 0xFFU);
    value[6] = (uint8_t)((offset_us >> 8) & 0xFFU);
    value[7] = (uint8_t)((offset_us >> 16) & 0xFFU);
    value[8] = (uint8_t)((offset_us >> 24) & 0xFFU);
    set_app_config_single_tlv(fd, session_id, UCI_APP_CONFIG_SESSION_TIME_BASE, value, 9, step_name);
}

static void init_session_dynamic(int fd, uint32_t session_id, const char* step_name) {
    uint8_t request[UCI_SIM_MAX_PACKET] = {0};
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    char message[160];

    request[0] = 0x21;
    request[1] = 0x00;
    request[2] = 0x00;
    request[3] = 0x05;
    request[4] = (uint8_t)(session_id & 0xFFU);
    request[5] = (uint8_t)((session_id >> 8) & 0xFFU);
    request[6] = (uint8_t)((session_id >> 16) & 0xFFU);
    request[7] = (uint8_t)((session_id >> 24) & 0xFFU);
    request[8] = UCI_SESSION_TYPE_RANGING;

    snprintf(message, sizeof(message), "%s write session init", step_name);
    ASSERT_TRUE(write_full(fd, request, 9) == 9, message);
    snprintf(message, sizeof(message), "%s read session init rsp", step_name);
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, message);
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse dynamic init rsp");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONFIG, parsed.gid, "dynamic init rsp gid");
    ASSERT_EQ_INT(UCI_SESSION_INIT, parsed.oid, "dynamic init rsp oid");
    ASSERT_EQ_INT(UCI_STATUS_OK, parsed.payload[0], "dynamic init rsp status");
    ASSERT_EQ_INT((int)session_id, (int)read_u32_le(&parsed.payload[1]), "dynamic init rsp session id");

    snprintf(message, sizeof(message), "%s read session init ntf", step_name);
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, message);
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse dynamic init ntf");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONFIG, parsed.gid, "dynamic init ntf gid");
    ASSERT_EQ_INT(UCI_SESSION_STATUS_NTF, parsed.oid, "dynamic init ntf oid");
    ASSERT_EQ_INT((int)session_id, (int)read_u32_le(parsed.payload), "dynamic init ntf session id");
    ASSERT_EQ_INT(UCI_SESSION_STATE_INIT, parsed.payload[4], "dynamic init ntf state");
}

static void start_session_dynamic(int fd, uint32_t session_id, const char* step_name) {
    uint8_t request[UCI_SIM_MAX_PACKET] = {0};
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    char message[160];

    request[0] = 0x22;
    request[1] = 0x00;
    request[2] = 0x00;
    request[3] = 0x04;
    request[4] = (uint8_t)(session_id & 0xFFU);
    request[5] = (uint8_t)((session_id >> 8) & 0xFFU);
    request[6] = (uint8_t)((session_id >> 16) & 0xFFU);
    request[7] = (uint8_t)((session_id >> 24) & 0xFFU);

    snprintf(message, sizeof(message), "%s write session start", step_name);
    ASSERT_TRUE(write_full(fd, request, 8) == 8, message);
    snprintf(message, sizeof(message), "%s read session start rsp", step_name);
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, message);
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse dynamic start rsp");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONTROL, parsed.gid, "dynamic start rsp gid");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "dynamic start rsp oid");
    ASSERT_EQ_INT(UCI_STATUS_OK, parsed.payload[0], "dynamic start rsp status");

    snprintf(message, sizeof(message), "%s read session start ntf", step_name);
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, message);
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse dynamic start ntf");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONFIG, parsed.gid, "dynamic start ntf gid");
    ASSERT_EQ_INT(UCI_SESSION_STATUS_NTF, parsed.oid, "dynamic start ntf oid");
    ASSERT_EQ_INT((int)session_id, (int)read_u32_le(parsed.payload), "dynamic start ntf session id");
    ASSERT_EQ_INT(UCI_SESSION_STATE_ACTIVE, parsed.payload[4], "dynamic start ntf state");
}

static void start_session_expect_failure_dynamic(int fd,
                                                 uint32_t session_id,
                                                 uint8_t expected_status,
                                                 const char* step_name) {
    uint8_t request[UCI_SIM_MAX_PACKET] = {0};
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    char message[160];

    request[0] = 0x22;
    request[1] = 0x00;
    request[2] = 0x00;
    request[3] = 0x04;
    request[4] = (uint8_t)(session_id & 0xFFU);
    request[5] = (uint8_t)((session_id >> 8) & 0xFFU);
    request[6] = (uint8_t)((session_id >> 16) & 0xFFU);
    request[7] = (uint8_t)((session_id >> 24) & 0xFFU);

    snprintf(message, sizeof(message), "%s write session start", step_name);
    ASSERT_TRUE(write_full(fd, request, 8) == 8, message);
    snprintf(message, sizeof(message), "%s read session start rsp", step_name);
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, message);
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse dynamic failed start rsp");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONTROL, parsed.gid, "dynamic failed start rsp gid");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "dynamic failed start rsp oid");
    ASSERT_EQ_INT(expected_status, parsed.payload[0], "dynamic failed start rsp status");
}

static int start_server(test_server_t* server) {
    uci_sim_engine_t engine;
    pid_t pid;
    static unsigned port_nonce = 0;

    server->port = (uint16_t)(20000 + (((unsigned)getpid() ^ (unsigned)time(NULL) ^ (++port_nonce * 7919U)) % 20000));
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        uci_sim_engine_init_with_scenario(&engine, server->scenario);
        if (uci_sim_tcp_serve("127.0.0.1", server->port, &engine) != 0) {
            _exit(1);
        }
        _exit(0);
    }

    server->pid = pid;
    return 0;
}

static void stop_server(test_server_t* server) {
    int status;
    int attempts = 0;

    if (server->pid <= 0) {
        return;
    }

    kill(server->pid, SIGTERM);
    /* Wait up to 3 seconds, then force-kill */
    while (attempts < 30) {
        pid_t rc = waitpid(server->pid, &status, WNOHANG);
        if (rc == server->pid) break;
        if (rc < 0) break;
        { struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL); }
        attempts++;
    }
    if (attempts >= 30) {
        kill(server->pid, SIGKILL);
        waitpid(server->pid, &status, 0);
    }
    server->pid = 0;
}

static int hex_value(int ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    ch = tolower((unsigned char)ch);
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    return -1;
}

static int load_hex_fixture(const char* path, uint8_t* buffer, size_t capacity, size_t* length) {
    FILE* fp;
    int ch;
    int high_nibble = -1;
    size_t out_len = 0;

    if (!path || !buffer || !length) {
        return -1;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    while ((ch = fgetc(fp)) != EOF) {
        int value;
        if (isspace((unsigned char)ch)) {
            continue;
        }
        value = hex_value(ch);
        if (value < 0) {
            fclose(fp);
            return -1;
        }
        if (high_nibble < 0) {
            high_nibble = value;
            continue;
        }
        if (out_len >= capacity) {
            fclose(fp);
            return -1;
        }
        buffer[out_len++] = (uint8_t)((high_nibble << 4) | value);
        high_nibble = -1;
    }

    fclose(fp);
    if (high_nibble >= 0) {
        return -1;
    }

    *length = out_len;
    return 0;
}

static size_t packet_stream_next_len(const uint8_t* buffer, size_t total_len, size_t offset) {
    size_t payload_len;

    if (!buffer || offset + UCI_SIM_HEADER_SIZE > total_len) {
        return 0;
    }

    payload_len = (((buffer[offset] >> 5) & 0x03) == UCI_MT_DATA)
        ? ((size_t)buffer[offset + 2] | ((size_t)buffer[offset + 3] << 8))
        : (size_t)buffer[offset + 3];
    if (offset + UCI_SIM_HEADER_SIZE + payload_len > total_len) {
        return 0;
    }

    return UCI_SIM_HEADER_SIZE + payload_len;
}

static int assert_fixture_packet(int fd, const char* fixture_path, const char* step_name) {
    uint8_t expected[UCI_SIM_MAX_PACKET];
    uint8_t actual[UCI_SIM_MAX_PACKET];
    size_t expected_len = 0;
    size_t actual_offset = 0;
    char message[160];

    /* Normal verification mode */
    snprintf(message, sizeof(message), "%s fixture load", step_name);
    if (load_hex_fixture(fixture_path, expected, sizeof(expected), &expected_len) != 0) {
        printf("FAIL: %s\n", message);
        g_failed++;
        return -1;
    }

    {
        size_t expected_offset = 0;
        while (expected_offset < expected_len) {
            size_t expected_packet_len = packet_stream_next_len(expected, expected_len, expected_offset);
            size_t actual_len = 0;

            snprintf(message, sizeof(message), "%s expected packet framing", step_name);
            if (!(expected_packet_len > 0)) { printf("FAIL: %s\n", message); g_failed++; return -1; }
            snprintf(message, sizeof(message), "%s packet read", step_name);
            if (read_packet(fd, actual + actual_offset, sizeof(actual) - actual_offset, &actual_len) != 0) {
                printf("FAIL: %s\n", message); g_failed++; return -1;
            }
            snprintf(message, sizeof(message), "%s packet length", step_name);
            if ((int)expected_packet_len != (int)actual_len) {
                printf("FAIL: %s (expected=%d, actual=%d)\n", message, (int)expected_packet_len, (int)actual_len);
                g_failed++; return -1;
            }
            snprintf(message, sizeof(message), "%s packet bytes", step_name);
            if (memcmp(expected + expected_offset, actual + actual_offset, actual_len) != 0) {
                size_t di;
                for (di = 0; di < actual_len; di++) {
                    if (expected[expected_offset + di] != actual[actual_offset + di]) break;
                }
                printf("FAIL: %s (byte %zu: exp=%02x act=%02x)\n", message, di,
                       expected[expected_offset + di], actual[actual_offset + di]);
                g_failed++; return -1;
            }
            expected_offset += expected_packet_len;
            actual_offset += actual_len;
        }
    }
    return 0;
}

static void assert_two_fixture_packets_any_order(int fd,
                                                 const char* fixture_a,
                                                 const char* fixture_b,
                                                 const char* step_name) {
    uint8_t expected_a[UCI_SIM_MAX_PACKET];
    uint8_t expected_b[UCI_SIM_MAX_PACKET];
    uint8_t actual_1[UCI_SIM_MAX_PACKET];
    uint8_t actual_2[UCI_SIM_MAX_PACKET];
    size_t expected_a_len = 0;
    size_t expected_b_len = 0;
    size_t actual_1_len = 0;
    size_t actual_2_len = 0;
    char message[160];
    int direct_match;

    snprintf(message, sizeof(message), "%s fixture A load", step_name);
    ASSERT_TRUE(load_hex_fixture(fixture_a, expected_a, sizeof(expected_a), &expected_a_len) == 0, message);
    snprintf(message, sizeof(message), "%s fixture B load", step_name);
    ASSERT_TRUE(load_hex_fixture(fixture_b, expected_b, sizeof(expected_b), &expected_b_len) == 0, message);
    snprintf(message, sizeof(message), "%s packet 1 read", step_name);
    ASSERT_TRUE(read_packet(fd, actual_1, sizeof(actual_1), &actual_1_len) == 0, message);
    snprintf(message, sizeof(message), "%s packet 2 read", step_name);
    ASSERT_TRUE(read_packet(fd, actual_2, sizeof(actual_2), &actual_2_len) == 0, message);

    direct_match = (actual_1_len == expected_a_len &&
                    actual_2_len == expected_b_len &&
                    memcmp(actual_1, expected_a, actual_1_len) == 0 &&
                    memcmp(actual_2, expected_b, actual_2_len) == 0);
    if (!direct_match) {
        int swapped_match = (actual_1_len == expected_b_len &&
                             actual_2_len == expected_a_len &&
                             memcmp(actual_1, expected_b, actual_1_len) == 0 &&
                             memcmp(actual_2, expected_a, actual_2_len) == 0);
        snprintf(message, sizeof(message), "%s packet ordering", step_name);
        ASSERT_TRUE(swapped_match, message);
    }
}

static void test_shell_compatible_core_and_session_flow_over_tcp(void) {
    static const tcp_interop_step_t k_steps[] = {
        {
            "tests/fixtures/tcp/core_device_info_cmd.hex",
            "tests/fixtures/tcp/core_device_info_rsp.hex",
            NULL,
            "core_device_info"
        },
        {
            "tests/fixtures/tcp/core_get_caps_info_cmd.hex",
            "tests/fixtures/tcp/core_get_caps_info_rsp.hex",
            NULL,
            "core_get_caps_info"
        },
        {
            "tests/fixtures/tcp/core_query_timestamp_cmd.hex",
            "tests/fixtures/tcp/core_query_timestamp_rsp.hex",
            NULL,
            "core_query_timestamp"
        },
        {
            "tests/fixtures/tcp/core_device_reset_cmd.hex",
            "tests/fixtures/tcp/core_device_reset_rsp.hex",
            "tests/fixtures/tcp/core_device_status_ready_ntf.hex",
            "core_device_reset"
        },
        {
            "tests/fixtures/tcp/core_set_config_device_state_cmd.hex",
            "tests/fixtures/tcp/core_set_config_device_state_rsp.hex",
            NULL,
            "core_set_config_device_state"
        },
        {
            "tests/fixtures/tcp/core_get_config_device_state_cmd.hex",
            "tests/fixtures/tcp/core_get_config_device_state_rsp.hex",
            NULL,
            "core_get_config_device_state"
        },
        {
            "tests/fixtures/tcp/core_set_config_low_power_mode_cmd.hex",
            "tests/fixtures/tcp/core_set_config_low_power_mode_rsp.hex",
            NULL,
            "core_set_config_low_power_mode"
        },
        {
            "tests/fixtures/tcp/core_get_config_low_power_mode_cmd.hex",
            "tests/fixtures/tcp/core_get_config_low_power_mode_rsp.hex",
            NULL,
            "core_get_config_low_power_mode"
        },
        {
            "tests/fixtures/tcp/core_set_config_device_pan_id_cmd.hex",
            "tests/fixtures/tcp/core_set_config_device_pan_id_rsp.hex",
            NULL,
            "core_set_config_device_pan_id"
        },
        {
            "tests/fixtures/tcp/core_get_config_device_pan_id_cmd.hex",
            "tests/fixtures/tcp/core_get_config_device_pan_id_rsp.hex",
            NULL,
            "core_get_config_device_pan_id"
        },
        {
            "tests/fixtures/tcp/session_init_cmd.hex",
            "tests/fixtures/tcp/session_init_rsp.hex",
            "tests/fixtures/tcp/session_init_ntf.hex",
            "session_init"
        },
        {
            "tests/fixtures/tcp/session_get_count_cmd.hex",
            "tests/fixtures/tcp/session_get_count_rsp.hex",
            NULL,
            "session_get_count"
        },
        {
            "tests/fixtures/tcp/session_query_data_size_cmd.hex",
            "tests/fixtures/tcp/session_query_data_size_rsp.hex",
            NULL,
            "session_query_data_size"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_multi_node_mode_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_multi_node_mode"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_device_role_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_device_role"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ranging_round_usage_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ranging_round_usage"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_sts_config_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_sts_config"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_channel_number_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_channel_number"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_no_of_controlee_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_no_of_controlee"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_device_mac_address_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_device_mac_address"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dst_mac_address_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dst_mac_address"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_slot_duration_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_slot_duration"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ranging_duration_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ranging_duration"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_sts_index_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_sts_index"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_mac_fcs_type_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_mac_fcs_type"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ranging_round_control_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ranging_round_control"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_aoa_result_req_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_aoa_result_req"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_rng_data_ntf_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_rng_data_ntf"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_rng_data_ntf_proximity_near_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_rng_data_ntf_proximity_near"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_rng_data_ntf_proximity_far_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_rng_data_ntf_proximity_far"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_result_report_config_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_result_report_config"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_in_band_termination_attempt_count_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_in_band_termination_attempt_count"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_bprf_phr_data_rate_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_bprf_phr_data_rate"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_max_number_of_measurements_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_max_number_of_measurements"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ul_tdoa_tx_interval_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ul_tdoa_tx_interval"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ul_tdoa_random_window_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ul_tdoa_random_window"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_sts_length_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_sts_length"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_suspend_ranging_rounds_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_suspend_ranging_rounds"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ul_tdoa_ntf_report_config_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ul_tdoa_ntf_report_config"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ul_tdoa_device_id_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ul_tdoa_device_id"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ul_tdoa_tx_timestamp_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ul_tdoa_tx_timestamp"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_anchor_cfo_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_anchor_cfo"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_anchor_location_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_anchor_location"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_tx_active_ranging_rounds_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_tx_active_ranging_rounds"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_block_striding_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_block_striding"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_time_reference_anchor_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_time_reference_anchor"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_session_key_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_session_key"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_subsession_key_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_subsession_key"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_session_data_transfer_status_ntf_config_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_session_data_transfer_status_ntf_config"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_session_time_base_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_session_time_base"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_responder_tof_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_responder_tof"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_secure_ranging_nefa_level_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_secure_ranging_nefa_level"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_secure_ranging_csw_length_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_secure_ranging_csw_length"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_application_data_endpoint_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_application_data_endpoint"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_owr_aoa_measurement_ntf_period_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_owr_aoa_measurement_ntf_period"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_min_frames_per_rr_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_min_frames_per_rr"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_mtu_size_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_mtu_size"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_inter_frame_interval_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_inter_frame_interval"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_ranging_method_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_ranging_method"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_rframe_config_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_rframe_config"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_rssi_reporting_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_rssi_reporting"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_sfd_id_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_sfd_id"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_psdu_data_rate_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_psdu_data_rate"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_preamble_duration_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_preamble_duration"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_link_layer_mode_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_link_layer_mode"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_data_repetition_count_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_data_repetition_count"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_block_stride_length_invalid_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_block_stride_length_invalid_rsp.hex",
            "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
            "session_set_app_config_block_stride_length_invalid"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_ranging_time_struct_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_ranging_time_struct"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_slots_per_rr_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_slots_per_rr"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_tx_adaptive_payload_power_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_tx_adaptive_payload_power"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_rng_data_ntf_aoa_bound_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_rng_data_ntf_aoa_bound"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_responder_slot_index_invalid_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_responder_slot_index_invalid_rsp.hex",
            "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
            "session_set_app_config_responder_slot_index_invalid"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_responder_slot_index_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_responder_slot_index"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_prf_mode_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_prf_mode"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_preamble_code_index_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_preamble_code_index"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_cap_size_range_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_cap_size_range"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_tx_jitter_window_size_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_tx_jitter_window_size"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_scheduled_mode_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_scheduled_mode"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_scheduled_mode_invalid_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_scheduled_mode_invalid_rsp.hex",
            "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
            "session_set_app_config_scheduled_mode_invalid"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_key_rotation_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_key_rotation"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_key_rotation_rate_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_key_rotation_rate"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_session_priority_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_session_priority"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_mac_address_mode_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_mac_address_mode"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_hopping_mode_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_hopping_mode"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rsp.hex",
            NULL,
            "session_get_app_config"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_multi_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_multi_rsp.hex",
            NULL,
            "session_get_app_config_multi"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ranging_round_usage_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ranging_round_usage_rsp.hex",
            NULL,
            "session_get_app_config_ranging_round_usage"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_sts_config_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_sts_config_rsp.hex",
            NULL,
            "session_get_app_config_sts_config"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_channel_number_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_channel_number_rsp.hex",
            NULL,
            "session_get_app_config_channel_number"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_no_of_controlee_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_no_of_controlee_rsp.hex",
            NULL,
            "session_get_app_config_no_of_controlee"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_device_mac_address_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_device_mac_address_rsp.hex",
            NULL,
            "session_get_app_config_device_mac_address"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dst_mac_address_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dst_mac_address_rsp.hex",
            NULL,
            "session_get_app_config_dst_mac_address"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_slot_duration_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_slot_duration_rsp.hex",
            NULL,
            "session_get_app_config_slot_duration"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ranging_duration_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ranging_duration_rsp.hex",
            NULL,
            "session_get_app_config_ranging_duration"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_sts_index_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_sts_index_rsp.hex",
            NULL,
            "session_get_app_config_sts_index"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_mac_fcs_type_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_mac_fcs_type_rsp.hex",
            NULL,
            "session_get_app_config_mac_fcs_type"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ranging_round_control_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ranging_round_control_rsp.hex",
            NULL,
            "session_get_app_config_ranging_round_control"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_aoa_result_req_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_aoa_result_req_rsp.hex",
            NULL,
            "session_get_app_config_aoa_result_req"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_rsp.hex",
            NULL,
            "session_get_app_config_rng_data_ntf"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_proximity_near_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_proximity_near_rsp.hex",
            NULL,
            "session_get_app_config_rng_data_ntf_proximity_near"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_proximity_far_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_proximity_far_rsp.hex",
            NULL,
            "session_get_app_config_rng_data_ntf_proximity_far"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_result_report_config_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_result_report_config_rsp.hex",
            NULL,
            "session_get_app_config_result_report_config"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_in_band_termination_attempt_count_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_in_band_termination_attempt_count_rsp.hex",
            NULL,
            "session_get_app_config_in_band_termination_attempt_count"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_bprf_phr_data_rate_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_bprf_phr_data_rate_rsp.hex",
            NULL,
            "session_get_app_config_bprf_phr_data_rate"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_max_number_of_measurements_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_max_number_of_measurements_rsp.hex",
            NULL,
            "session_get_app_config_max_number_of_measurements"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_tx_interval_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_tx_interval_rsp.hex",
            NULL,
            "session_get_app_config_ul_tdoa_tx_interval"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_random_window_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_random_window_rsp.hex",
            NULL,
            "session_get_app_config_ul_tdoa_random_window"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_sts_length_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_sts_length_rsp.hex",
            NULL,
            "session_get_app_config_sts_length"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_suspend_ranging_rounds_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_suspend_ranging_rounds_rsp.hex",
            NULL,
            "session_get_app_config_suspend_ranging_rounds"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_ntf_report_config_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_ntf_report_config_rsp.hex",
            NULL,
            "session_get_app_config_ul_tdoa_ntf_report_config"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_device_id_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_device_id_rsp.hex",
            NULL,
            "session_get_app_config_ul_tdoa_device_id"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_tx_timestamp_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ul_tdoa_tx_timestamp_rsp.hex",
            NULL,
            "session_get_app_config_ul_tdoa_tx_timestamp"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_anchor_cfo_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_anchor_cfo_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_anchor_cfo"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_anchor_location_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_anchor_location_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_anchor_location"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_tx_active_ranging_rounds_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_tx_active_ranging_rounds_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_tx_active_ranging_rounds"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_block_striding_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_block_striding_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_block_striding"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_time_reference_anchor_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_time_reference_anchor_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_time_reference_anchor"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_session_key_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_session_key_rsp.hex",
            NULL,
            "session_get_app_config_session_key"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_subsession_key_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_subsession_key_rsp.hex",
            NULL,
            "session_get_app_config_subsession_key"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_session_data_transfer_status_ntf_config_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_session_data_transfer_status_ntf_config_rsp.hex",
            NULL,
            "session_get_app_config_session_data_transfer_status_ntf_config"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_session_time_base_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_session_time_base_rsp.hex",
            NULL,
            "session_get_app_config_session_time_base"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_responder_tof_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_responder_tof_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_responder_tof"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_secure_ranging_nefa_level_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_secure_ranging_nefa_level_rsp.hex",
            NULL,
            "session_get_app_config_secure_ranging_nefa_level"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_secure_ranging_csw_length_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_secure_ranging_csw_length_rsp.hex",
            NULL,
            "session_get_app_config_secure_ranging_csw_length"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_application_data_endpoint_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_application_data_endpoint_rsp.hex",
            NULL,
            "session_get_app_config_application_data_endpoint"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_owr_aoa_measurement_ntf_period_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_owr_aoa_measurement_ntf_period_rsp.hex",
            NULL,
            "session_get_app_config_owr_aoa_measurement_ntf_period"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_min_frames_per_rr_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_min_frames_per_rr_rsp.hex",
            NULL,
            "session_get_app_config_min_frames_per_rr"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_mtu_size_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_mtu_size_rsp.hex",
            NULL,
            "session_get_app_config_mtu_size"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_inter_frame_interval_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_inter_frame_interval_rsp.hex",
            NULL,
            "session_get_app_config_inter_frame_interval"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_ranging_method_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_ranging_method_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_ranging_method"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_vendor_id_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_vendor_id"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_static_sts_iv_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_static_sts_iv"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_number_of_sts_segments_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_number_of_sts_segments"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_max_rr_retry_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_max_rr_retry"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_uwb_initiation_time_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_uwb_initiation_time"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_block_stride_length_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_block_stride_length"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_sub_session_id_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_sub_session_id"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_tx_timestamp_conf_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_tx_timestamp_conf"
        },
        {
            "tests/fixtures/tcp/session_set_app_config_dl_tdoa_hop_count_cmd.hex",
            "tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_dl_tdoa_hop_count"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_tx_timestamp_conf_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_tx_timestamp_conf_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_tx_timestamp_conf"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_hop_count_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_dl_tdoa_hop_count_rsp.hex",
            NULL,
            "session_get_app_config_dl_tdoa_hop_count"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_vendor_id_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_vendor_id_rsp.hex",
            NULL,
            "session_get_app_config_vendor_id"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_static_sts_iv_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_static_sts_iv_rsp.hex",
            NULL,
            "session_get_app_config_static_sts_iv"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_number_of_sts_segments_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_number_of_sts_segments_rsp.hex",
            NULL,
            "session_get_app_config_number_of_sts_segments"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_max_rr_retry_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_max_rr_retry_rsp.hex",
            NULL,
            "session_get_app_config_max_rr_retry"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_uwb_initiation_time_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_uwb_initiation_time_rsp.hex",
            NULL,
            "session_get_app_config_uwb_initiation_time"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_block_stride_length_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_block_stride_length_rsp.hex",
            NULL,
            "session_get_app_config_block_stride_length"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_sub_session_id_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_sub_session_id_rsp.hex",
            NULL,
            "session_get_app_config_sub_session_id"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_rframe_config_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rframe_config_rsp.hex",
            NULL,
            "session_get_app_config_rframe_config"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_rssi_reporting_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rssi_reporting_rsp.hex",
            NULL,
            "session_get_app_config_rssi_reporting"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_preamble_code_index_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_preamble_code_index_rsp.hex",
            NULL,
            "session_get_app_config_preamble_code_index"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_sfd_id_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_sfd_id_rsp.hex",
            NULL,
            "session_get_app_config_sfd_id"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_psdu_data_rate_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_psdu_data_rate_rsp.hex",
            NULL,
            "session_get_app_config_psdu_data_rate"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_preamble_duration_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_preamble_duration_rsp.hex",
            NULL,
            "session_get_app_config_preamble_duration"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_link_layer_mode_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_link_layer_mode_rsp.hex",
            NULL,
            "session_get_app_config_link_layer_mode"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_data_repetition_count_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_data_repetition_count_rsp.hex",
            NULL,
            "session_get_app_config_data_repetition_count"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_ranging_time_struct_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_ranging_time_struct_rsp.hex",
            NULL,
            "session_get_app_config_ranging_time_struct"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_slots_per_rr_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_slots_per_rr_rsp.hex",
            NULL,
            "session_get_app_config_slots_per_rr"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_tx_adaptive_payload_power_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_tx_adaptive_payload_power_rsp.hex",
            NULL,
            "session_get_app_config_tx_adaptive_payload_power"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_aoa_bound_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_rng_data_ntf_aoa_bound_rsp.hex",
            NULL,
            "session_get_app_config_rng_data_ntf_aoa_bound"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_responder_slot_index_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_responder_slot_index_rsp.hex",
            NULL,
            "session_get_app_config_responder_slot_index"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_prf_mode_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_prf_mode_rsp.hex",
            NULL,
            "session_get_app_config_prf_mode"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_cap_size_range_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_cap_size_range_rsp.hex",
            NULL,
            "session_get_app_config_cap_size_range"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_tx_jitter_window_size_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_tx_jitter_window_size_rsp.hex",
            NULL,
            "session_get_app_config_tx_jitter_window_size"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_scheduled_mode_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_scheduled_mode_rsp.hex",
            NULL,
            "session_get_app_config_scheduled_mode"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_key_rotation_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_key_rotation_rsp.hex",
            NULL,
            "session_get_app_config_key_rotation"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_key_rotation_rate_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_key_rotation_rate_rsp.hex",
            NULL,
            "session_get_app_config_key_rotation_rate"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_session_priority_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_session_priority_rsp.hex",
            NULL,
            "session_get_app_config_session_priority"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_mac_address_mode_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_mac_address_mode_rsp.hex",
            NULL,
            "session_get_app_config_mac_address_mode"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_hopping_mode_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_hopping_mode_rsp.hex",
            NULL,
            "session_get_app_config_hopping_mode"
        },
        {
            "tests/fixtures/tcp/session_get_app_config_all_cmd.hex",
            "tests/fixtures/tcp/session_get_app_config_all_rsp.hex",
            NULL,
            "session_get_app_config_all"
        },
        {
            "tests/fixtures/tcp/session_update_multicast_add_cmd.hex",
            "tests/fixtures/tcp/session_update_multicast_add_rsp.hex",
            NULL,
            "session_update_multicast_add"
        },
        {
            "tests/fixtures/tcp/session_update_multicast_remove_cmd.hex",
            "tests/fixtures/tcp/session_update_multicast_remove_rsp.hex",
            NULL,
            "session_update_multicast_remove"
        },
        {
            "tests/fixtures/tcp/session_update_dt_anchor_rounds_cmd.hex",
            "tests/fixtures/tcp/session_update_dt_anchor_rounds_rsp.hex",
            NULL,
            "session_update_dt_anchor_rounds"
        },
        {
            "tests/fixtures/tcp/session_update_dt_tag_rounds_cmd.hex",
            "tests/fixtures/tcp/session_update_dt_tag_rounds_rsp.hex",
            NULL,
            "session_update_dt_tag_rounds"
        },
        {
            "tests/fixtures/tcp/session_set_hus_controller_config_cmd.hex",
            "tests/fixtures/tcp/session_set_hus_controller_config_rsp.hex",
            NULL,
            "session_set_hus_controller_config"
        },
        {
            "tests/fixtures/tcp/session_set_hus_controlee_config_cmd.hex",
            "tests/fixtures/tcp/session_set_hus_controlee_config_rsp.hex",
            NULL,
            "session_set_hus_controlee_config"
        },
        {
            "tests/fixtures/tcp/session_start_cmd.hex",
            "tests/fixtures/tcp/session_start_rsp.hex",
            "tests/fixtures/tcp/session_start_ntf.hex",
            "session_start"
        },
        {
            "tests/fixtures/tcp/session_data_transfer_phase_config_cmd.hex",
            "tests/fixtures/tcp/session_data_transfer_phase_config_rsp.hex",
            NULL,
            "session_data_transfer_phase_config"
        },
        {
            "tests/fixtures/tcp/session_logical_link_create_cmd.hex",
            "tests/fixtures/tcp/session_logical_link_create_rsp.hex",
            "tests/fixtures/tcp/session_logical_link_uwbs_create_ntf.hex",
            "session_logical_link_create"
        },
        {
            "tests/fixtures/tcp/session_logical_link_get_param_cmd.hex",
            "tests/fixtures/tcp/session_logical_link_get_param_rsp.hex",
            NULL,
            "session_logical_link_get_param"
        },
        {
            "tests/fixtures/tcp/session_logical_link_close_cmd.hex",
            "tests/fixtures/tcp/session_logical_link_close_rsp.hex",
            "tests/fixtures/tcp/session_logical_link_uwbs_close_ntf.hex",
            "session_logical_link_close"
        },
        {
            "tests/fixtures/tcp/session_get_state_cmd.hex",
            "tests/fixtures/tcp/session_get_state_rsp.hex",
            NULL,
            "session_get_state"
        },
        {
            "tests/fixtures/tcp/session_stop_cmd.hex",
            "tests/fixtures/tcp/session_stop_rsp.hex",
            "tests/fixtures/tcp/session_stop_ntf.hex",
            "session_stop"
        },
        {
            "tests/fixtures/tcp/session_get_ranging_count_cmd.hex",
            "tests/fixtures/tcp/session_get_ranging_count_rsp.hex",
            NULL,
            "session_get_ranging_count"
        }
    };

    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    int fd = -1;
    size_t i;
    char message[160];

    ASSERT_TRUE(start_server(&server) == 0, "start_server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect_with_retry");

    for (i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); ++i) {
        snprintf(message, sizeof(message), "%s request load", k_steps[i].step_name);
        ASSERT_TRUE(load_hex_fixture(k_steps[i].request_fixture, request, sizeof(request), &request_len) == 0, message);
        snprintf(message, sizeof(message), "%s write", k_steps[i].step_name);
        ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, message);

        assert_fixture_packet(fd, k_steps[i].response_fixture, k_steps[i].step_name);
        if (k_steps[i].notification_fixture) {
            char notification_step[160];
            snprintf(notification_step, sizeof(notification_step), "%s notification", k_steps[i].step_name);
            assert_fixture_packet(fd, k_steps[i].notification_fixture, notification_step);
        }
        if (g_failed > 0) break;
    }

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_delayed_notification_flow_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DELAYED_NOTIFICATIONS;
    ASSERT_TRUE(start_server(&server) == 0, "start delayed server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect delayed server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load delayed session_init request");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write delayed session_init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "delayed session_init rsp");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_get_state_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load delayed get_state request");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write delayed get_state");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_get_state_init_rsp.hex",
                          "delayed session_get_state rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "delayed session_init ntf");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_core_generic_error_flow_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    int fd = -1;

    ASSERT_TRUE(start_server(&server) == 0, "start generic error server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect generic error server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/core_set_config_invalid_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load invalid core set_config");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write invalid core set_config");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_set_config_invalid_rsp.hex",
                          "invalid core set_config rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
                          "invalid core set_config generic error ntf");

    close(fd);
    stop_server(&server);
    PASS();
}


static void test_ranging_stream_disable_info_ntf_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start ranging disable server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect ranging disable server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging disable init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging disable init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "ranging disable init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "ranging disable init ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_set_app_config_rng_data_ntf_disable_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging disable app config");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging disable app config");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "ranging disable app config rsp");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging disable start");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging disable start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "ranging disable start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "ranging disable start ntf");

    ASSERT_TRUE(set_socket_timeout_ms(fd, 150) == 0, "set ranging disable timeout");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) != 0, "ranging disable should not emit range data");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_proximity_inside_mode_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start proximity-inside server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect proximity-inside server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load proximity-inside init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write proximity-inside init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "proximity-inside init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "proximity-inside init ntf");

    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x0E;
    request[10] = 0x01;
    request[11] = 0x02;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write proximity-inside ntf mode");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "proximity-inside ntf mode rsp");
    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_set_app_config_rng_data_ntf_proximity_near_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load proximity-inside near");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write proximity-inside near");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "proximity-inside near rsp");
    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x09;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x10;
    request[10] = 0x02;
    request[11] = 0x69;
    request[12] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 13) == 13, "write proximity-inside far");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "proximity-inside far rsp");

    set_ranging_interval_ms(fd, 50U, "proximity-inside");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load proximity-inside start");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write proximity-inside start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "proximity-inside start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "proximity-inside start ntf");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read proximity-inside range 1");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse proximity-inside range 1");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "proximity-inside range 1 oid");
    ASSERT_EQ_INT(100, read_u16_le(&parsed.payload[29]), "proximity-inside range 1 distance");
    ASSERT_EQ_INT(50, (int)read_u32_le(&parsed.payload[9]), "proximity-inside range 1 interval");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read proximity-inside range 2");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse proximity-inside range 2");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "proximity-inside range 2 oid");
    ASSERT_EQ_INT(105, read_u16_le(&parsed.payload[29]), "proximity-inside range 2 distance");

    ASSERT_TRUE(set_socket_timeout_ms(fd, 200) == 0, "set proximity-inside timeout");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) != 0, "proximity-inside should suppress out-of-range notification");
    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_result_report_config_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start result-report server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect result-report server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load result-report init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write result-report init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "result-report init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "result-report init ntf");

    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x2E;
    request[10] = 0x01;
    request[11] = 0x01;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write result-report config");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "result-report config rsp");

    set_ranging_interval_ms(fd, 50U, "result-report");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load result-report start");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write result-report start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "result-report start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "result-report start ntf");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read result-report range packet");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse result-report range packet");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "result-report range oid");
    ASSERT_EQ_INT(100, read_u16_le(&parsed.payload[29]), "result-report should preserve distance");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[31]), "result-report should suppress local azimuth");
    ASSERT_EQ_INT(0, parsed.payload[33], "result-report should suppress local azimuth fom");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[34]), "result-report should suppress local elevation");
    ASSERT_EQ_INT(0, parsed.payload[36], "result-report should suppress local elevation fom");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[37]), "result-report should suppress remote azimuth");
    ASSERT_EQ_INT(0, parsed.payload[39], "result-report should suppress remote azimuth fom");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[40]), "result-report should suppress remote elevation");
    ASSERT_EQ_INT(0, parsed.payload[42], "result-report should suppress remote elevation fom");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_aoa_result_req_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start aoa-result server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect aoa-result server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load aoa-result init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write aoa-result init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "aoa-result init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "aoa-result init ntf");

    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x2E;
    request[10] = 0x01;
    request[11] = 0x0F;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write aoa-result report config");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "aoa-result report config rsp");

    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x0D;
    request[10] = 0x01;
    request[11] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write aoa-result req config");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "aoa-result req config rsp");

    set_ranging_interval_ms(fd, 50U, "aoa-result");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load aoa-result start");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write aoa-result start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "aoa-result start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "aoa-result start ntf");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read aoa-result range packet");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse aoa-result range packet");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "aoa-result range oid");
    ASSERT_EQ_INT(100, read_u16_le(&parsed.payload[29]), "aoa-result should preserve distance");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[31]), "aoa-result should suppress local azimuth");
    ASSERT_EQ_INT(0, parsed.payload[33], "aoa-result should suppress local azimuth fom");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[34]), "aoa-result should suppress local elevation");
    ASSERT_EQ_INT(0, parsed.payload[36], "aoa-result should suppress local elevation fom");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[37]), "aoa-result should suppress remote azimuth");
    ASSERT_EQ_INT(0, parsed.payload[39], "aoa-result should suppress remote azimuth fom");
    ASSERT_EQ_INT(0, read_u16_le(&parsed.payload[40]), "aoa-result should suppress remote elevation");
    ASSERT_EQ_INT(0, parsed.payload[42], "aoa-result should suppress remote elevation fom");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_rssi_reporting_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start rssi-report server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect rssi-report server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load rssi-report init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write rssi-report init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "rssi-report init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "rssi-report init ntf");

    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x13;
    request[10] = 0x01;
    request[11] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write rssi-report config");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "rssi-report config rsp");

    set_ranging_interval_ms(fd, 50U, "rssi-report");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load rssi-report start");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write rssi-report start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "rssi-report start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "rssi-report start ntf");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read rssi-report range packet");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse rssi-report range packet");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "rssi-report range oid");
    ASSERT_EQ_INT(0, parsed.payload[44], "rssi-report should suppress rssi");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_ranging_interval_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start ranging-interval server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect ranging-interval server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load ranging-interval init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write ranging-interval init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "ranging-interval init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "ranging-interval init ntf");

    set_ranging_interval_ms(fd, 50U, "ranging-interval");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load ranging-interval start");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write ranging-interval start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "ranging-interval start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "ranging-interval start ntf");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read ranging-interval range packet");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse ranging-interval range packet");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "ranging-interval range oid");
    ASSERT_EQ_INT(50, (int)read_u32_le(&parsed.payload[9]), "ranging-interval should affect emitted packet");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_max_number_of_measurements_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start max-measurements server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect max-measurements server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load max-measurements init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write max-measurements init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "max-measurements init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "max-measurements init ntf");

    set_ranging_interval_ms(fd, 50U, "max-measurements");
    set_max_number_of_measurements(fd, 2U, "max-measurements");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load max-measurements start");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write max-measurements start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "max-measurements start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "max-measurements start ntf");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read max-measurements range 1");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse max-measurements range 1");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "max-measurements range 1 oid");
    ASSERT_EQ_INT(1, (int)read_u32_le(parsed.payload), "max-measurements range 1 sequence");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read max-measurements range 2");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse max-measurements range 2");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "max-measurements range 2 oid");
    ASSERT_EQ_INT(2, (int)read_u32_le(parsed.payload), "max-measurements range 2 sequence");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read max-measurements status ntf");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse max-measurements status ntf");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONFIG, parsed.gid, "max-measurements status gid");
    ASSERT_EQ_INT(UCI_SESSION_STATUS_NTF, parsed.oid, "max-measurements status oid");
    ASSERT_EQ_INT(UCI_SESSION_STATE_IDLE, parsed.payload[4], "max-measurements idle state");
    ASSERT_EQ_INT(UCI_SESSION_REASON_MAX_NUMBER_OF_MEASUREMENTS_REACHED,
                  parsed.payload[5],
                  "max-measurements reason");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_get_ranging_count_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load max-measurements count");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write max-measurements count");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read max-measurements count rsp");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse max-measurements count rsp");
    ASSERT_EQ_INT(UCI_SESSION_GET_RANGING_COUNT, parsed.oid, "max-measurements count oid");
    ASSERT_EQ_INT(0, parsed.payload[0], "max-measurements count status");
    ASSERT_EQ_INT(2, (int)read_u32_le(&parsed.payload[1]), "max-measurements count value");

    ASSERT_TRUE(set_socket_timeout_ms(fd, 100) == 0, "set max-measurements timeout");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) != 0,
                "max-measurements should stop after budget exhaustion");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_session_time_base_reference_alignment_over_tcp(void) {
    test_server_t server = {0};
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t packet_len = 0;
    int fd = -1;
    const uint32_t reference_session_id = 0x11111111U;
    const uint32_t dependent_session_id = 0x22222222U;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start session_time_base server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect session_time_base server");
    ASSERT_TRUE(set_socket_timeout_ms(fd, 500) == 0, "set session_time_base timeout");

    init_session_dynamic(fd, reference_session_id, "session_time_base ref");
    init_session_dynamic(fd, dependent_session_id, "session_time_base dep");
    set_ranging_interval_ms_for_session(fd, reference_session_id, 50U, "session_time_base ref");
    set_ranging_interval_ms_for_session(fd, dependent_session_id, 50U, "session_time_base dep");
    start_session_dynamic(fd, reference_session_id, "session_time_base ref");
    /* Drain initial range data from reference session (engine tick fires immediately) */
    {
        uint8_t drain_buf[512]; size_t drain_len;
        ASSERT_TRUE(set_socket_timeout_ms(fd, 100) == 0, "set session_time_base drain timeout");
        while (read_packet(fd, drain_buf, sizeof(drain_buf), &drain_len) == 0) {}
        ASSERT_TRUE(set_socket_timeout_ms(fd, 500) == 0, "restore session_time_base timeout");
    }
    set_session_time_base(fd,
                          dependent_session_id,
                          0x01U,
                          reference_session_id,
                          20000U,
                          "session_time_base dep");
    start_session_dynamic(fd, dependent_session_id, "session_time_base dep");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_time_base reference range");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse session_time_base reference range");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "session_time_base first range oid");
    /* With immediate engine ticks, both sessions may fire range data simultaneously.
     * Accept either session ID as the first range packet. */
    {
        uint32_t first_sid = read_u32_le(&parsed.payload[4]);
        ASSERT_TRUE(first_sid == reference_session_id || first_sid == dependent_session_id,
                    "session_time_base first range should be reference or dependent session");
    }

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_time_base dependent range");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse session_time_base dependent range");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "session_time_base second range oid");
    ASSERT_EQ_INT((int)dependent_session_id,
                  (int)read_u32_le(&parsed.payload[4]),
                  "session_time_base second range should be dependent session");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_session_time_base_rejects_ranging_interval_mismatch_over_tcp(void) {
    test_server_t server = {0};
    int fd = -1;
    const uint32_t reference_session_id = 0x11111111U;
    const uint32_t dependent_session_id = 0x22222222U;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start session_time_base mismatch server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect session_time_base mismatch server");

    init_session_dynamic(fd, reference_session_id, "session_time_base mismatch ref");
    init_session_dynamic(fd, dependent_session_id, "session_time_base mismatch dep");
    set_ranging_interval_ms_for_session(fd, reference_session_id, 50U, "session_time_base mismatch ref");
    set_ranging_interval_ms_for_session(fd, dependent_session_id, 60U, "session_time_base mismatch dep");
    start_session_dynamic(fd, reference_session_id, "session_time_base mismatch ref");
    /* Drain initial range data from reference session */
    {
        uint8_t drain_buf[512]; size_t drain_len;
        ASSERT_TRUE(set_socket_timeout_ms(fd, 100) == 0, "set mismatch drain timeout");
        while (read_packet(fd, drain_buf, sizeof(drain_buf), &drain_len) == 0) {}
        ASSERT_TRUE(set_socket_timeout_ms(fd, 500) == 0, "restore mismatch timeout");
    }
    set_session_time_base(fd,
                          dependent_session_id,
                          0x01U,
                          reference_session_id,
                          20000U,
                          "session_time_base mismatch dep");
    start_session_expect_failure_dynamic(fd,
                                         dependent_session_id,
                                         UCI_STATUS_INVALID_PARAM,
                                         "session_time_base mismatch dep");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_session_time_base_rejects_offset_outside_interval_over_tcp(void) {
    test_server_t server = {0};
    int fd = -1;
    const uint32_t reference_session_id = 0x11111111U;
    const uint32_t dependent_session_id = 0x22222222U;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start session_time_base offset server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect session_time_base offset server");

    init_session_dynamic(fd, reference_session_id, "session_time_base offset ref");
    init_session_dynamic(fd, dependent_session_id, "session_time_base offset dep");
    set_ranging_interval_ms_for_session(fd, reference_session_id, 50U, "session_time_base offset ref");
    set_ranging_interval_ms_for_session(fd, dependent_session_id, 50U, "session_time_base offset dep");
    start_session_dynamic(fd, reference_session_id, "session_time_base offset ref");
    /* Drain initial range data from reference session */
    {
        uint8_t drain_buf[512]; size_t drain_len;
        ASSERT_TRUE(set_socket_timeout_ms(fd, 100) == 0, "set offset drain timeout");
        while (read_packet(fd, drain_buf, sizeof(drain_buf), &drain_len) == 0) {}
        ASSERT_TRUE(set_socket_timeout_ms(fd, 500) == 0, "restore offset timeout");
    }
    set_session_time_base(fd,
                          dependent_session_id,
                          0x01U,
                          reference_session_id,
                          50000U,
                          "session_time_base offset dep");
    start_session_expect_failure_dynamic(fd,
                                         dependent_session_id,
                                         UCI_STATUS_INVALID_PARAM,
                                         "session_time_base offset dep");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_interval_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x05, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x05 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-interval server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-interval server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-interval init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-interval init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-interval init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-interval init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x0B;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_RANGING_INTERVAL;
    request[10] = 0x04;
    request[11] = 49;
    request[12] = 0x00;
    request[13] = 0x00;
    request[14] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 15) == 15, "write invalid-interval set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-interval rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-interval rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-interval rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-interval generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-interval generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-interval generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_slot_duration_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-slot-duration server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-slot-duration server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-slot-duration init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-slot-duration init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-slot-duration init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-slot-duration init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x09;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_SLOT_DURATION;
    request[10] = 0x02;
    request[11] = 0x0F;
    request[12] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 13) == 13, "write invalid-slot-duration set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-slot-duration rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-slot-duration rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-slot-duration rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-slot-duration generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-slot-duration generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-slot-duration generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_result_report_config_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-result-report server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-result-report server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-result-report init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-result-report init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-result-report init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-result-report init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_RESULT_REPORT_CONFIG;
    request[10] = 0x01;
    request[11] = 0x10;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-result-report set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-result-report rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-result-report rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-result-report rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-result-report generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-result-report generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-result-report generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_cap_size_range_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-cap-size server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-cap-size server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-cap-size init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-cap-size init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-cap-size init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-cap-size init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x09;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_CAP_SIZE_RANGE;
    request[10] = 0x02;
    request[11] = 0x10;
    request[12] = 0x05;
    ASSERT_TRUE(write_full(fd, request, 13) == 13, "write invalid-cap-size set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-cap-size rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-cap-size rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-cap-size rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-cap-size generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-cap-size generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-cap-size generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_aoa_result_req_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-aoa-result server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-aoa-result server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-aoa-result init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-aoa-result init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-aoa-result init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-aoa-result init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_AOA_RESULT_REQ;
    request[10] = 0x01;
    request[11] = 0x04;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-aoa-result set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-aoa-result rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-aoa-result rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-aoa-result rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-aoa-result generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-aoa-result generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-aoa-result generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_rssi_reporting_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-rssi-reporting server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-rssi-reporting server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-rssi-reporting init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-rssi-reporting init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-rssi-reporting init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-rssi-reporting init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_RSSI_REPORTING;
    request[10] = 0x01;
    request[11] = 0x02;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-rssi-reporting set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-rssi-reporting rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-rssi-reporting rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-rssi-reporting rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-rssi-reporting generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-rssi-reporting generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-rssi-reporting generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_round_usage_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-round-usage server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-round-usage server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-round-usage init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-round-usage init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-round-usage init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-round-usage init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_RANGING_ROUND_USAGE;
    request[10] = 0x01;
    request[11] = 0x05;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-round-usage set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-round-usage rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-round-usage rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-round-usage rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-round-usage generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-round-usage generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-round-usage generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_device_type_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-device-type server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-device-type server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-device-type init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-device-type init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-device-type init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-device-type init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_DEVICE_TYPE;
    request[10] = 0x01;
    request[11] = 0x02;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-device-type set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-device-type rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-device-type rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-device-type rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-device-type generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-device-type generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-device-type generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}


static void test_multi_node_mode_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-multi-node-mode server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-multi-node-mode server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-multi-node-mode init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-multi-node-mode init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-multi-node-mode init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-multi-node-mode init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_MULTI_NODE_MODE;
    request[10] = 0x01;
    request[11] = 0x03;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-multi-node-mode set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-multi-node-mode rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-multi-node-mode rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-multi-node-mode rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-multi-node-mode generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-multi-node-mode generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-multi-node-mode generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_number_of_controlees_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-number-of-controlees server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-number-of-controlees server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-number-of-controlees init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-number-of-controlees init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-number-of-controlees init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-number-of-controlees init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_NUMBER_OF_CONTROLEES;
    request[10] = 0x01;
    request[11] = 0x09;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-number-of-controlees set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-number-of-controlees rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-number-of-controlees rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-number-of-controlees rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-number-of-controlees generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-number-of-controlees generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-number-of-controlees generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_mac_address_mode_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-mac-address-mode server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-mac-address-mode server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-mac-address-mode init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-mac-address-mode init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-mac-address-mode init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-mac-address-mode init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_MAC_ADDRESS_MODE;
    request[10] = 0x01;
    request[11] = UCI_MAC_ADDRESS_MODE_EXTENDED;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-mac-address-mode set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-mac-address-mode rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-mac-address-mode rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-mac-address-mode rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-mac-address-mode generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-mac-address-mode generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-mac-address-mode generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_channel_number_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-channel-number server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-channel-number server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-channel-number init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-channel-number init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-channel-number init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-channel-number init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_CHANNEL_NUMBER;
    request[10] = 0x01;
    request[11] = 0x06;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-channel-number set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-channel-number rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-channel-number rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-channel-number rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-channel-number generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-channel-number generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-channel-number generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_prf_mode_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-prf-mode server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-prf-mode server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-prf-mode init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-prf-mode init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-prf-mode init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-prf-mode init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_PRF_MODE;
    request[10] = 0x01;
    request[11] = 0x03;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-prf-mode set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-prf-mode rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-prf-mode rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-prf-mode rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-prf-mode generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-prf-mode generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-prf-mode generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_preamble_code_index_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-preamble-code-index server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-preamble-code-index server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-preamble-code-index init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-preamble-code-index init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-preamble-code-index init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-preamble-code-index init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_PRF_MODE;
    request[10] = 0x01;
    request[11] = 0x01;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-preamble-code-index prf mode");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "invalid-preamble-code-index prf rsp");

    request[9] = UCI_APP_CONFIG_PREAMBLE_CODE_INDEX;
    request[11] = 0x18;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-preamble-code-index set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-preamble-code-index rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-preamble-code-index rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-preamble-code-index rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-preamble-code-index generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-preamble-code-index generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-preamble-code-index generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_sfd_id_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-sfd-id server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-sfd-id server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-sfd-id init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-sfd-id init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-sfd-id init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-sfd-id init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_PRF_MODE;
    request[10] = 0x01;
    request[11] = 0x01;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-sfd-id prf mode");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "invalid-sfd-id prf rsp");

    request[9] = UCI_APP_CONFIG_SFD_ID;
    request[11] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-sfd-id set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-sfd-id rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-sfd-id rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-sfd-id rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-sfd-id generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-sfd-id generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-sfd-id generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_psdu_data_rate_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-psdu-data-rate server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-psdu-data-rate server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-psdu-data-rate init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-psdu-data-rate init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-psdu-data-rate init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-psdu-data-rate init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_PSDU_DATA_RATE;
    request[10] = 0x01;
    request[11] = 0x04;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-psdu-data-rate set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-psdu-data-rate rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-psdu-data-rate rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-psdu-data-rate rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-psdu-data-rate generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-psdu-data-rate generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-psdu-data-rate generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_preamble_duration_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-preamble-duration server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-preamble-duration server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-preamble-duration init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-preamble-duration init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-preamble-duration init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-preamble-duration init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_PREAMBLE_DURATION;
    request[10] = 0x01;
    request[11] = 0x02;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-preamble-duration set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-preamble-duration rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-preamble-duration rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-preamble-duration rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-preamble-duration generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-preamble-duration generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-preamble-duration generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_sts_length_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-sts-length server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-sts-length server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-sts-length init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-sts-length init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-sts-length init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-sts-length init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_STS_LENGTH;
    request[10] = 0x01;
    request[11] = 0x03;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-sts-length set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-sts-length rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-sts-length rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-sts-length rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-sts-length generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-sts-length generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-sts-length generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_key_rotation_rate_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-key-rotation-rate server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-key-rotation-rate server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-key-rotation-rate init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-key-rotation-rate init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-key-rotation-rate init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-key-rotation-rate init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x09;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_KEY_ROTATION_RATE;
    request[10] = 0x02;
    request[11] = 0x10;
    request[12] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 13) == 13, "write invalid-key-rotation-rate set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-key-rotation-rate rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-key-rotation-rate rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-key-rotation-rate rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-key-rotation-rate generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-key-rotation-rate generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-key-rotation-rate generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_key_rotation_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-key-rotation server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-key-rotation server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-key-rotation init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-key-rotation init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-key-rotation init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-key-rotation init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_KEY_ROTATION;
    request[10] = 0x01;
    request[11] = 0x02;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-key-rotation set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-key-rotation rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-key-rotation rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-key-rotation rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-key-rotation generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-key-rotation generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-key-rotation generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_number_of_sts_segments_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-number-of-sts-segments server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-number-of-sts-segments server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-number-of-sts-segments init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-number-of-sts-segments init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-number-of-sts-segments init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-number-of-sts-segments init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_NUMBER_OF_STS_SEGMENTS;
    request[10] = 0x01;
    request[11] = 0x05;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-number-of-sts-segments set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-number-of-sts-segments rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-number-of-sts-segments rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-number-of-sts-segments rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-number-of-sts-segments generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-number-of-sts-segments generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-number-of-sts-segments generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}


static void test_max_rr_retry_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start max rr retry validation server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect max rr retry validation server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load max rr retry validation init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write max rr retry validation init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "max rr retry validation init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "max rr retry validation init ntf");

    memset(request, 0, sizeof(request));
    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = 0x2A;
    request[10] = 0x01;
    request[11] = 0x04;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid max rr retry config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid max rr retry rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid max rr retry rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid max rr retry rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid max rr retry generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid max rr retry generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid max rr retry generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_link_layer_mode_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-link-layer-mode server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-link-layer-mode server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-link-layer-mode init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-link-layer-mode init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-link-layer-mode init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-link-layer-mode init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_LINK_LAYER_MODE;
    request[10] = 0x01;
    request[11] = 0x02;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-link-layer-mode set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-link-layer-mode rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-link-layer-mode rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-link-layer-mode rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-link-layer-mode generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-link-layer-mode generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-link-layer-mode generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_device_mac_address_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-device-mac-address server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-device-mac-address server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-device-mac-address init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-device-mac-address init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-device-mac-address init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-device-mac-address init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x0B;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_DEVICE_MAC_ADDRESS;
    request[10] = 0x04;
    request[11] = 0xAA;
    request[12] = 0xBB;
    request[13] = 0xCC;
    request[14] = 0xDD;
    ASSERT_TRUE(write_full(fd, request, 15) == 15, "write invalid-device-mac-address set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-device-mac-address rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-device-mac-address rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-device-mac-address rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-device-mac-address generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-device-mac-address generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-device-mac-address generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_dst_mac_address_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-dst-mac-address server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-dst-mac-address server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-dst-mac-address init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-dst-mac-address init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-dst-mac-address init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-dst-mac-address init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x0A;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_DST_MAC_ADDRESS;
    request[10] = 0x03;
    request[11] = 0x78;
    request[12] = 0x56;
    request[13] = 0x34;
    ASSERT_TRUE(write_full(fd, request, 14) == 14, "write invalid-dst-mac-address set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-dst-mac-address rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-dst-mac-address rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-dst-mac-address rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-dst-mac-address generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-dst-mac-address generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-dst-mac-address generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_sts_config_validation_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    static const uint8_t expected_response[] = { 0x41, 0x03, 0x00, 0x02, 0x04, 0x00 };
    static const uint8_t expected_notification[] = { 0x60, 0x07, 0x00, 0x01, 0x04 };
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start invalid-sts-config server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect invalid-sts-config server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &packet_len) == 0,
                "load invalid-sts-config init");
    ASSERT_TRUE(write_full(fd, request, packet_len) == (ssize_t)packet_len, "write invalid-sts-config init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "invalid-sts-config init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "invalid-sts-config init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_STS_CONFIG;
    request[10] = 0x01;
    request[11] = 0x05;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write invalid-sts-config set app config");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-sts-config rsp");
    ASSERT_EQ_INT((int)sizeof(expected_response), (int)packet_len, "invalid-sts-config rsp size");
    ASSERT_MEMEQ(expected_response, packet, sizeof(expected_response), "invalid-sts-config rsp bytes");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read invalid-sts-config generic error");
    ASSERT_EQ_INT((int)sizeof(expected_notification), (int)packet_len, "invalid-sts-config generic error size");
    ASSERT_MEMEQ(expected_notification, packet, sizeof(expected_notification), "invalid-sts-config generic error bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_flow_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t parsed;
    size_t request_len = 0;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start ranging stream server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect ranging stream server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "ranging stream init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "ranging stream init ntf");

    set_ranging_interval_ms(fd, 50U, "ranging stream flow");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream start");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "ranging stream start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "ranging stream start ntf");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read ranging stream range packet 1");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse ranging stream range packet 1");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "ranging stream range 1 oid");
    ASSERT_EQ_INT(1, (int)read_u32_le(parsed.payload), "ranging stream range 1 sequence");
    ASSERT_EQ_INT(50, (int)read_u32_le(&parsed.payload[9]), "ranging stream range 1 interval");

    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read ranging stream range packet 2");
    ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed) == 0, "parse ranging stream range packet 2");
    ASSERT_EQ_INT(UCI_SESSION_START, parsed.oid, "ranging stream range 2 oid");
    ASSERT_EQ_INT(2, (int)read_u32_le(parsed.payload), "ranging stream range 2 sequence");
    ASSERT_EQ_INT(50, (int)read_u32_le(&parsed.payload[9]), "ranging stream range 2 interval");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_stop_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream stop");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream stop");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_stop_rsp.hex",
                          "ranging stream stop rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_stop_ntf.hex",
                          "ranging stream stop ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_get_ranging_count_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream count");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream count");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_get_ranging_count_after_stream_rsp.hex",
                          "ranging stream count rsp");

    ASSERT_TRUE(set_socket_timeout_ms(fd, 100) == 0, "set ranging stream timeout");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) != 0, "ranging stream should stop after session stop");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_data_message_edge_cases_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start data edge server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect data edge server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data edge init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data edge init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "data edge init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "data edge init ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/data_message_send_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load inactive data send");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write inactive data send");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_data_transfer_status_rejected_ntf.hex",
                          "inactive data send status");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data edge start");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data edge start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "data edge start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "data edge start ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/data_message_send_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load repeated data send");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write repeated data send 1");
    assert_two_fixture_packets_any_order(fd,
                          "tests/fixtures/tcp/session_data_credit_ntf.hex",
                          "tests/fixtures/tcp/session_data_transfer_status_ntf.hex",
                          "repeated data send first pair");

    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write repeated data send 2");
    assert_two_fixture_packets_any_order(fd,
                          "tests/fixtures/tcp/session_data_credit_ntf.hex",
                          "tests/fixtures/tcp/session_data_transfer_status_repetition_ntf.hex",
                          "repeated data send second pair");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_data_message_repetition_progress_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start data repetition server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect data repetition server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data repetition init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data repetition init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "data repetition init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "data repetition init ntf");

    request[0] = 0x21;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x08;
    request[4] = 0x78;
    request[5] = 0x56;
    request[6] = 0x34;
    request[7] = 0x12;
    request[8] = 0x01;
    request[9] = UCI_APP_CONFIG_DATA_REPETITION_COUNT;
    request[10] = 0x01;
    request[11] = 0x01;
    ASSERT_TRUE(write_full(fd, request, 12) == 12, "write data repetition count");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "data repetition count rsp");

    request[3] = 0x0B;
    request[9] = UCI_APP_CONFIG_RANGING_INTERVAL;
    request[10] = 0x04;
    request[11] = 0x32;
    request[12] = 0x00;
    request[13] = 0x00;
    request[14] = 0x00;
    ASSERT_TRUE(write_full(fd, request, 15) == 15, "write data repetition interval");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_rsp.hex",
                          "data repetition interval rsp");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data repetition start");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data repetition start");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_rsp.hex",
                          "data repetition start rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_start_ntf.hex",
                          "data repetition start ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/data_message_send_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data repetition send");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data repetition send");
    /* With poll-based TCP server, engine ticks fire events immediately.
     * First send may complete before the 30ms quiet window. */
    ASSERT_TRUE(set_socket_timeout_ms(fd, 30) == 0, "set data repetition quiet timeout");
    if (read_packet(fd, packet, sizeof(packet), &packet_len) == 0) {
        /* First send completed immediately (engine tick fired the event) */
        uci_sim_packet_t parsed2;
        ASSERT_TRUE(uci_sim_packet_parse(packet, packet_len, &parsed2) == 0, "parse data repetition early status");
        ASSERT_EQ_INT(UCI_SESSION_DATA_TRANSFER_STATUS_NTF, parsed2.oid, "data repetition early oid");
    }
    /* else: deferred (original behavior) — continue with overlapping send */

    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data repetition overlapping send");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read data repetition overlapping status");
    ASSERT_EQ_INT(UCI_SESSION_DATA_TRANSFER_STATUS_NTF, packet[1] & 0x3FU, "data repetition overlapping oid");
    ASSERT_EQ_INT(UCI_DATA_TRANSFER_STATUS_ERROR_DATA_TRANSFER_IS_ONGOING, packet[10], "data repetition overlapping status");

    ASSERT_TRUE(set_socket_timeout_ms(fd, 300) == 0, "set data repetition event timeout");
    /* Consume DATA_TRANSFER_STATUS + DATA_CREDIT in any order */
    {
        int got_status = 0, got_credit = 0;
        for (int attempt = 0; attempt < 4 && (!got_status || !got_credit); attempt++) {
            ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read data repetition event");
            uint8_t oid = packet[1] & 0x3FU;
            if (oid == UCI_SESSION_DATA_TRANSFER_STATUS_NTF) {
                uint8_t st = packet[10];
                ASSERT_TRUE(st == UCI_DATA_TRANSFER_STATUS_REPETITION_OK || st == UCI_DATA_TRANSFER_STATUS_OK,
                            "data repetition status");
                ASSERT_TRUE(packet[11] == 1 || packet[11] == 2, "data repetition tx count");
                got_status = 1;
            } else if (oid == UCI_SESSION_DATA_CREDIT_NTF) {
                got_credit = 1;
            }
        }
        ASSERT_TRUE(got_status, "data repetition missing status");
        ASSERT_TRUE(got_credit, "data repetition missing credit");
    }

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_control_edge_cases_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_DEFAULT;
    ASSERT_TRUE(start_server(&server) == 0, "start control edge server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect control edge server");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load control edge init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write control edge init");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_rsp.hex",
                          "control edge init rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_init_ntf.hex",
                          "control edge init ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_update_multicast_invalid_action_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load multicast invalid action");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write multicast invalid action");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_update_multicast_invalid_action_rsp.hex",
                          "multicast invalid action rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
                          "multicast invalid action generic error ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_data_transfer_phase_config_missing_session_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load dtp missing session");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write dtp missing session");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_data_transfer_phase_config_missing_session_rsp.hex",
                          "dtp missing session rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
                          "dtp missing session generic error ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_set_app_config_ranging_time_struct_invalid_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging_time_struct invalid");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging_time_struct invalid");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_set_app_config_ranging_time_struct_invalid_rsp.hex",
                          "ranging_time_struct invalid rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_generic_error_invalid_param_ntf.hex",
                          "ranging_time_struct invalid generic error ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_logical_link_close_short_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load logical link short close");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write logical link short close");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_logical_link_close_short_rsp.hex",
                          "logical link short close rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_generic_error_invalid_msg_size_ntf.hex",
                          "logical link short close generic error ntf");

    ASSERT_TRUE(load_hex_fixture("tests/fixtures/tcp/session_logical_link_create_short_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load logical link short create");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write logical link short create");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/session_logical_link_create_short_rsp.hex",
                          "logical link short create rsp");
    assert_fixture_packet(fd,
                          "tests/fixtures/tcp/core_generic_error_invalid_msg_size_ntf.hex",
                          "logical link short create generic error ntf");

    close(fd);
    stop_server(&server);
    PASS();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== TCP interop test starting ===\n");
#define RUN_TEST(fn) do { printf("  TEST: %s...\n", #fn); fflush(stdout); fn(); } while(0)
    RUN_TEST(test_shell_compatible_core_and_session_flow_over_tcp);
    RUN_TEST(test_delayed_notification_flow_over_tcp);
    RUN_TEST(test_core_generic_error_flow_over_tcp);
    RUN_TEST(test_ranging_stream_disable_info_ntf_over_tcp);
    RUN_TEST(test_ranging_stream_proximity_inside_mode_over_tcp);
    RUN_TEST(test_ranging_stream_result_report_config_over_tcp);
    RUN_TEST(test_ranging_stream_aoa_result_req_over_tcp);
    RUN_TEST(test_ranging_stream_rssi_reporting_over_tcp);
    RUN_TEST(test_ranging_stream_ranging_interval_over_tcp);
    RUN_TEST(test_ranging_stream_max_number_of_measurements_over_tcp);
    RUN_TEST(test_session_time_base_reference_alignment_over_tcp);
    RUN_TEST(test_session_time_base_rejects_ranging_interval_mismatch_over_tcp);
    RUN_TEST(test_session_time_base_rejects_offset_outside_interval_over_tcp);
    RUN_TEST(test_ranging_interval_validation_over_tcp);
    RUN_TEST(test_slot_duration_validation_over_tcp);
    RUN_TEST(test_result_report_config_validation_over_tcp);
    RUN_TEST(test_cap_size_range_validation_over_tcp);
    RUN_TEST(test_number_of_controlees_validation_over_tcp);
    RUN_TEST(test_mac_address_mode_validation_over_tcp);
    RUN_TEST(test_device_mac_address_validation_over_tcp);
    RUN_TEST(test_dst_mac_address_validation_over_tcp);
    RUN_TEST(test_sts_config_validation_over_tcp);
    RUN_TEST(test_aoa_result_req_validation_over_tcp);
    RUN_TEST(test_rssi_reporting_validation_over_tcp);
    RUN_TEST(test_ranging_round_usage_validation_over_tcp);
    RUN_TEST(test_device_type_validation_over_tcp);
    RUN_TEST(test_multi_node_mode_validation_over_tcp);
    RUN_TEST(test_channel_number_validation_over_tcp);
    RUN_TEST(test_prf_mode_validation_over_tcp);
    RUN_TEST(test_preamble_code_index_validation_over_tcp);
    RUN_TEST(test_sfd_id_validation_over_tcp);
    RUN_TEST(test_psdu_data_rate_validation_over_tcp);
    RUN_TEST(test_data_message_repetition_progress_over_tcp);
    RUN_TEST(test_preamble_duration_validation_over_tcp);
    RUN_TEST(test_sts_length_validation_over_tcp);
    RUN_TEST(test_key_rotation_validation_over_tcp);
    RUN_TEST(test_max_rr_retry_validation_over_tcp);
    RUN_TEST(test_number_of_sts_segments_validation_over_tcp);
    RUN_TEST(test_key_rotation_rate_validation_over_tcp);
    RUN_TEST(test_link_layer_mode_validation_over_tcp);
    RUN_TEST(test_ranging_stream_flow_over_tcp);
    RUN_TEST(test_data_message_edge_cases_over_tcp);
    RUN_TEST(test_control_edge_cases_over_tcp);

    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
