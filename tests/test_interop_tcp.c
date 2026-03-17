#define _POSIX_C_SOURCE 200809L

#include "uci_sim_device.h"
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

static int start_server(test_server_t* server) {
    uci_sim_device_t device;
    pid_t pid;

    server->port = (uint16_t)(20000 + (((unsigned)getpid() ^ (unsigned)time(NULL)) % 20000));
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        uci_sim_device_init_with_scenario(&device, server->scenario);
        if (uci_sim_tcp_serve("127.0.0.1", server->port, &device) != 0) {
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
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_init_ntf.hex",
            "session_init"
        },
        {
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_cmd.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_rsp.hex",
            "/media/chpo/HDD-papa/gemini_test/uci_device_simulator/tests/fixtures/tcp/session_start_ntf.hex",
            "session_start"
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

int main(void) {
    test_shell_compatible_core_and_session_flow_over_tcp();
    test_delayed_notification_flow_over_tcp();

    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
