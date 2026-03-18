#include "uci_sim_tcp_server.h"
#include "uci_sim_packet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define UCI_SIM_ENGINE_TICK_MS 10

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

static int flush_engine_outbound(int client_fd,
                                 uint8_t* buffer,
                                 size_t buffer_capacity,
                                 uci_sim_engine_t* engine) {
    uci_sim_packet_t packet;

    while (uci_sim_engine_dequeue_outbound_packet(engine, &packet) == 0) {
        if (write_packet(client_fd, buffer, buffer_capacity, &packet) != 0) {
            return -1;
        }
    }

    return 0;
}

static int process_client(int client_fd, uci_sim_engine_t* engine) {
    uint8_t header[UCI_SIM_HEADER_SIZE];
    uint8_t buffer[UCI_SIM_MAX_PACKET];

    while (1) {
        uci_sim_packet_t request;
        size_t packet_len;
        ssize_t rc;
        fd_set read_fds;
        struct timeval timeout;

        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = UCI_SIM_ENGINE_TICK_MS * 1000;

        rc = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            if (uci_sim_engine_tick(engine, UCI_SIM_ENGINE_TICK_MS) != 0) {
                return -1;
            }
            if (flush_engine_outbound(client_fd, buffer, sizeof(buffer), engine) != 0) {
                return -1;
            }
            continue;
        }

        rc = read_full(client_fd, header, sizeof(header));
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
        if (uci_sim_engine_submit_packet(engine, &request) != 0) {
            return -1;
        }
        if (flush_engine_outbound(client_fd, buffer, sizeof(buffer), engine) != 0) {
            return -1;
        }
    }
}

int uci_sim_tcp_serve(const char* host, uint16_t port, uci_sim_engine_t* engine) {
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

    if (process_client(client_fd, engine) != 0) {
        fprintf(stderr, "Client processing failed: %s\n", strerror(errno));
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}
