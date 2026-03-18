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
#define ASSERT_EQ_INT(exp, act, msg) do { if ((exp) != (act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_MEMEQ(exp, act, len, msg) do { if (memcmp((exp), (act), (len)) != 0) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
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

static int start_server(test_server_t* server) {
    uci_sim_engine_t engine;
    pid_t pid;

    server->port = (uint16_t)(20000 + (((unsigned)getpid() ^ (unsigned)time(NULL)) % 20000));
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

    if (server->pid <= 0) {
        return;
    }

    kill(server->pid, SIGTERM);
    waitpid(server->pid, &status, 0);
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

static void assert_fixture_packet(int fd, const char* fixture_path, const char* step_name) {
    uint8_t expected[UCI_SIM_MAX_PACKET];
    uint8_t actual[UCI_SIM_MAX_PACKET];
    size_t expected_len = 0;
    size_t actual_len = 0;
    char message[160];

    snprintf(message, sizeof(message), "%s fixture load", step_name);
    ASSERT_TRUE(load_hex_fixture(fixture_path, expected, sizeof(expected), &expected_len) == 0, message);
    snprintf(message, sizeof(message), "%s packet read", step_name);
    ASSERT_TRUE(read_packet(fd, actual, sizeof(actual), &actual_len) == 0, message);
    snprintf(message, sizeof(message), "%s length", step_name);
    ASSERT_EQ_INT((int)expected_len, (int)actual_len, message);
    snprintf(message, sizeof(message), "%s bytes", step_name);
    ASSERT_MEMEQ(expected, actual, actual_len, message);
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
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_device_info_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_device_info_rsp.hex",
            NULL,
            "core_device_info"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_caps_info_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_caps_info_rsp.hex",
            NULL,
            "core_get_caps_info"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_query_timestamp_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_query_timestamp_rsp.hex",
            NULL,
            "core_query_timestamp"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_device_reset_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_device_reset_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_device_status_ready_ntf.hex",
            "core_device_reset"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_set_config_device_state_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_set_config_device_state_rsp.hex",
            NULL,
            "core_set_config_device_state"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_config_device_state_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_config_device_state_rsp.hex",
            NULL,
            "core_get_config_device_state"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_set_config_low_power_mode_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_set_config_low_power_mode_rsp.hex",
            NULL,
            "core_set_config_low_power_mode"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_config_low_power_mode_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_config_low_power_mode_rsp.hex",
            NULL,
            "core_get_config_low_power_mode"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_set_config_device_pan_id_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_set_config_device_pan_id_rsp.hex",
            NULL,
            "core_set_config_device_pan_id"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_config_device_pan_id_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/core_get_config_device_pan_id_rsp.hex",
            NULL,
            "core_get_config_device_pan_id"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_ntf.hex",
            "session_init"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_count_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_count_rsp.hex",
            NULL,
            "session_get_count"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_query_data_size_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_query_data_size_rsp.hex",
            NULL,
            "session_query_data_size"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_set_app_config_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_set_app_config_multi_node_mode_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_multi_node_mode"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_set_app_config_device_role_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_set_app_config_rsp.hex",
            NULL,
            "session_set_app_config_device_role"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_app_config_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_app_config_rsp.hex",
            NULL,
            "session_get_app_config"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_app_config_multi_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_app_config_multi_rsp.hex",
            NULL,
            "session_get_app_config_multi"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_app_config_all_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_app_config_all_rsp.hex",
            NULL,
            "session_get_app_config_all"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_update_multicast_add_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_update_multicast_add_rsp.hex",
            NULL,
            "session_update_multicast_add"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_update_multicast_remove_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_update_multicast_remove_rsp.hex",
            NULL,
            "session_update_multicast_remove"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_ntf.hex",
            "session_start"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_phase_config_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_phase_config_rsp.hex",
            NULL,
            "session_data_transfer_phase_config"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/data_message_send_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_credit_ntf.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_status_ntf.hex",
            "data_message_send"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_create_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_create_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_uwbs_create_ntf.hex",
            "session_logical_link_create"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_get_param_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_get_param_rsp.hex",
            NULL,
            "session_logical_link_get_param"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_close_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_close_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_uwbs_close_ntf.hex",
            "session_logical_link_close"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_state_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_state_rsp.hex",
            NULL,
            "session_get_state"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_stop_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_stop_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_stop_ntf.hex",
            "session_stop"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_ranging_count_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_ranging_count_rsp.hex",
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

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load delayed session_init request");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write delayed session_init");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_rsp.hex",
                          "delayed session_init rsp");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_state_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load delayed get_state request");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write delayed get_state");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_state_init_rsp.hex",
                          "delayed session_get_state rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_ntf.hex",
                          "delayed session_init ntf");

    close(fd);
    stop_server(&server);
    PASS();
}

static void test_ranging_stream_flow_over_tcp(void) {
    test_server_t server = {0};
    uint8_t request[UCI_SIM_MAX_PACKET];
    uint8_t packet[UCI_SIM_MAX_PACKET];
    size_t request_len = 0;
    size_t packet_len = 0;
    int fd = -1;

    server.scenario = UCI_SIM_SCENARIO_RANGING_STREAM;
    ASSERT_TRUE(start_server(&server) == 0, "start ranging stream server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect ranging stream server");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream init");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_rsp.hex",
                          "ranging stream init rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_ntf.hex",
                          "ranging stream init ntf");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream start");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream start");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_rsp.hex",
                          "ranging stream start rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_ntf.hex",
                          "ranging stream start ntf");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_range_data_ntf_1.hex",
                          "ranging stream range ntf 1");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_state_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream get state");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream get state");
    assert_two_fixture_packets_any_order(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_state_rsp.hex",
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_range_data_ntf_2.hex",
                          "ranging stream get state pair");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_stop_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream stop");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream stop");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_stop_rsp.hex",
                          "ranging stream stop rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_stop_ntf.hex",
                          "ranging stream stop ntf");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_ranging_count_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load ranging stream count");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write ranging stream count");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_get_ranging_count_after_stream_rsp.hex",
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

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data edge init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data edge init");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_rsp.hex",
                          "data edge init rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_ntf.hex",
                          "data edge init ntf");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/data_message_send_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load inactive data send");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write inactive data send");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_status_rejected_ntf.hex",
                          "inactive data send status");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load data edge start");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write data edge start");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_rsp.hex",
                          "data edge start rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_ntf.hex",
                          "data edge start ntf");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/data_message_send_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load repeated data send");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write repeated data send 1");
    assert_two_fixture_packets_any_order(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_credit_ntf.hex",
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_status_ntf.hex",
                          "repeated data send first pair");

    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write repeated data send 2");
    assert_two_fixture_packets_any_order(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_credit_ntf.hex",
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_status_repetition_ntf.hex",
                          "repeated data send second pair");

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

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load control edge init");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write control edge init");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_rsp.hex",
                          "control edge init rsp");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_ntf.hex",
                          "control edge init ntf");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_update_multicast_invalid_action_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load multicast invalid action");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write multicast invalid action");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_update_multicast_invalid_action_rsp.hex",
                          "multicast invalid action rsp");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_phase_config_missing_session_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load dtp missing session");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write dtp missing session");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_data_transfer_phase_config_missing_session_rsp.hex",
                          "dtp missing session rsp");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_close_short_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load logical link short close");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write logical link short close");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_close_short_rsp.hex",
                          "logical link short close rsp");

    ASSERT_TRUE(load_hex_fixture("/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_create_short_cmd.hex",
                                 request,
                                 sizeof(request),
                                 &request_len) == 0,
                "load logical link short create");
    ASSERT_TRUE(write_full(fd, request, request_len) == (ssize_t)request_len, "write logical link short create");
    assert_fixture_packet(fd,
                          "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_logical_link_create_short_rsp.hex",
                          "logical link short create rsp");

    close(fd);
    stop_server(&server);
    PASS();
}

int main(void) {
    test_shell_compatible_core_and_session_flow_over_tcp();
    test_delayed_notification_flow_over_tcp();
    test_ranging_stream_flow_over_tcp();
    test_data_message_edge_cases_over_tcp();
    test_control_edge_cases_over_tcp();

    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
