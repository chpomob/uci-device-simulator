#define _POSIX_C_SOURCE 200809L

#include "uci_sim_packet.h"
#include "uci_sim_tcp_server.h"

#include <arpa/inet.h>
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
} test_server_t;

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

    server->port = (uint16_t)(19000 + (getpid() % 1000));
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        uci_sim_device_init(&device);
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

static void test_shell_compatible_core_and_session_flow_over_tcp(void) {
    static const uint8_t k_core_device_info_cmd[] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t k_expected_core_device_info_rsp[] = {
        0x40, 0x02, 0x00, 0x09,
        0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01
    };
    static const uint8_t k_core_get_caps_info_cmd[] = { 0x20, 0x03, 0x00, 0x00 };
    static const uint8_t k_expected_core_get_caps_info_rsp[] = {
        0x40, 0x03, 0x00, 0x04,
        0x00, 0x01, 0xE4, 0x00
    };
    static const uint8_t k_session_init_cmd[] = {
        0x21, 0x00, 0x00, 0x05,
        0x78, 0x56, 0x34, 0x12, 0x00
    };
    static const uint8_t k_expected_session_init_rsp[] = {
        0x41, 0x00, 0x00, 0x05,
        0x00, 0x78, 0x56, 0x34, 0x12
    };
    static const uint8_t k_expected_session_init_ntf[] = {
        0x61, 0x02, 0x00, 0x06,
        0x78, 0x56, 0x34, 0x12, 0x00, 0x00
    };
    static const uint8_t k_session_start_cmd[] = {
        0x22, 0x00, 0x00, 0x04,
        0x78, 0x56, 0x34, 0x12
    };
    static const uint8_t k_expected_session_start_rsp[] = {
        0x42, 0x00, 0x00, 0x01,
        0x00
    };
    static const uint8_t k_expected_session_start_ntf[] = {
        0x61, 0x02, 0x00, 0x06,
        0x78, 0x56, 0x34, 0x12, 0x01, 0x00
    };
    static const uint8_t k_session_stop_cmd[] = {
        0x22, 0x01, 0x00, 0x04,
        0x78, 0x56, 0x34, 0x12
    };
    static const uint8_t k_expected_session_stop_rsp[] = {
        0x42, 0x01, 0x00, 0x01,
        0x00
    };
    static const uint8_t k_expected_session_stop_ntf[] = {
        0x61, 0x02, 0x00, 0x06,
        0x78, 0x56, 0x34, 0x12, 0x02, 0x00
    };
    static const uint8_t k_session_get_state_cmd[] = {
        0x21, 0x06, 0x00, 0x04,
        0x78, 0x56, 0x34, 0x12
    };
    static const uint8_t k_expected_session_get_state_rsp[] = {
        0x41, 0x06, 0x00, 0x02,
        0x00, 0x01
    };

    test_server_t server = {0};
    uint8_t packet[UCI_SIM_MAX_PACKET];
    size_t packet_len = 0;
    int fd = -1;

    ASSERT_TRUE(start_server(&server) == 0, "start_server");
    fd = connect_with_retry(server.port);
    ASSERT_TRUE(fd >= 0, "connect_with_retry");

    ASSERT_TRUE(write_full(fd, k_core_device_info_cmd, sizeof(k_core_device_info_cmd)) == (ssize_t)sizeof(k_core_device_info_cmd),
                "write core_device_info");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read core_device_info rsp");
    ASSERT_EQ_INT((int)sizeof(k_expected_core_device_info_rsp), (int)packet_len, "core_device_info rsp len");
    ASSERT_MEMEQ(k_expected_core_device_info_rsp, packet, packet_len, "core_device_info rsp bytes");

    ASSERT_TRUE(write_full(fd, k_core_get_caps_info_cmd, sizeof(k_core_get_caps_info_cmd)) == (ssize_t)sizeof(k_core_get_caps_info_cmd),
                "write core_get_caps_info");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read core_get_caps_info rsp");
    ASSERT_EQ_INT((int)sizeof(k_expected_core_get_caps_info_rsp), (int)packet_len, "core_get_caps_info rsp len");
    ASSERT_MEMEQ(k_expected_core_get_caps_info_rsp, packet, packet_len, "core_get_caps_info rsp bytes");

    ASSERT_TRUE(write_full(fd, k_session_init_cmd, sizeof(k_session_init_cmd)) == (ssize_t)sizeof(k_session_init_cmd),
                "write session_init");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_init rsp");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_init_rsp), (int)packet_len, "session_init rsp len");
    ASSERT_MEMEQ(k_expected_session_init_rsp, packet, packet_len, "session_init rsp bytes");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_init ntf");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_init_ntf), (int)packet_len, "session_init ntf len");
    ASSERT_MEMEQ(k_expected_session_init_ntf, packet, packet_len, "session_init ntf bytes");

    ASSERT_TRUE(write_full(fd, k_session_start_cmd, sizeof(k_session_start_cmd)) == (ssize_t)sizeof(k_session_start_cmd),
                "write session_start");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_start rsp");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_start_rsp), (int)packet_len, "session_start rsp len");
    ASSERT_MEMEQ(k_expected_session_start_rsp, packet, packet_len, "session_start rsp bytes");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_start ntf");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_start_ntf), (int)packet_len, "session_start ntf len");
    ASSERT_MEMEQ(k_expected_session_start_ntf, packet, packet_len, "session_start ntf bytes");

    ASSERT_TRUE(write_full(fd, k_session_get_state_cmd, sizeof(k_session_get_state_cmd)) == (ssize_t)sizeof(k_session_get_state_cmd),
                "write session_get_state");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_get_state rsp");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_get_state_rsp), (int)packet_len, "session_get_state rsp len");
    ASSERT_MEMEQ(k_expected_session_get_state_rsp, packet, packet_len, "session_get_state rsp bytes");

    ASSERT_TRUE(write_full(fd, k_session_stop_cmd, sizeof(k_session_stop_cmd)) == (ssize_t)sizeof(k_session_stop_cmd),
                "write session_stop");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_stop rsp");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_stop_rsp), (int)packet_len, "session_stop rsp len");
    ASSERT_MEMEQ(k_expected_session_stop_rsp, packet, packet_len, "session_stop rsp bytes");
    ASSERT_TRUE(read_packet(fd, packet, sizeof(packet), &packet_len) == 0, "read session_stop ntf");
    ASSERT_EQ_INT((int)sizeof(k_expected_session_stop_ntf), (int)packet_len, "session_stop ntf len");
    ASSERT_MEMEQ(k_expected_session_stop_ntf, packet, packet_len, "session_stop ntf bytes");

    close(fd);
    stop_server(&server);
    PASS();
}

int main(void) {
    test_shell_compatible_core_and_session_flow_over_tcp();
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
