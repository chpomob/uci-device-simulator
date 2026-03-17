#include "uci_sim_tcp_server.h"
#include "uci_sim_packet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

static int write_packet(int fd, uint8_t* buffer, size_t buffer_capacity, const uci_sim_packet_t* packet) {
    size_t written = 0;

    if (uci_sim_packet_serialize(packet, buffer, buffer_capacity, &written) != 0) {
        return -1;
    }
    if (write_full(fd, buffer, written) <= 0) {
        return -1;
    }
    return 0;
}

static int process_client(int client_fd, uci_sim_device_t* device) {
    uint8_t header[UCI_SIM_HEADER_SIZE];
    uint8_t buffer[UCI_SIM_MAX_PACKET];

    while (1) {
        uci_sim_packet_t request;
        uci_sim_result_t result;
        size_t packet_len;
        ssize_t rc = read_full(client_fd, header, sizeof(header));
        if (rc == 0) {
            return 0;
        }
        if (rc < 0) {
            return -1;
        }

        request.mt = (header[0] >> 5) & 0x03;
        packet_len = (request.mt == UCI_MT_DATA)
            ? ((size_t)header[2] | ((size_t)header[3] << 8))
            : (size_t)header[3];
        memcpy(buffer, header, sizeof(header));
        if (packet_len > 0) {
            rc = read_full(client_fd, buffer + UCI_SIM_HEADER_SIZE, packet_len);
            if (rc <= 0) {
                return -1;
            }
        }

        if (uci_sim_packet_parse(buffer, UCI_SIM_HEADER_SIZE + packet_len, &request) != 0) {
            return -1;
        }
        if (uci_sim_device_handle_packet(device, &request, &result) != 0 && !result.has_response) {
            return -1;
        }

        if (result.has_response) {
            if (write_packet(client_fd, buffer, sizeof(buffer), &result.response) != 0) {
                return -1;
            }
        }
        if (result.has_notification) {
            if (write_packet(client_fd, buffer, sizeof(buffer), &result.notification) != 0) {
                return -1;
            }
        }
        if (!uci_sim_scenario_should_defer_notification(device->scenario)) {
            uci_sim_packet_t pending_notification;
            while (uci_sim_device_dequeue_notification(device, &pending_notification) == 0) {
                if (write_packet(client_fd, buffer, sizeof(buffer), &pending_notification) != 0) {
                    return -1;
                }
            }
        }
    }
}

int uci_sim_tcp_serve(const char* host, uint16_t port, uci_sim_device_t* device) {
    int listen_fd;
    int client_fd;
    int reuse_addr = 1;
    struct sockaddr_in addr;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) != 0) {
        perror("setsockopt");
        close(listen_fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (host == NULL || strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid listen host: %s\n", host);
        close(listen_fd);
        return -1;
    }
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }
    if (listen(listen_fd, 1) != 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    printf("UCI simulator listening on %s:%u\n", host, (unsigned int)port);
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(listen_fd);
        return -1;
    }

    if (process_client(client_fd, device) != 0) {
        fprintf(stderr, "Client processing failed: %s\n", strerror(errno));
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}
