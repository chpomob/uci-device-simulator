#define _POSIX_C_SOURCE 200809L

/*
 * uci_sim_tcp_server.c
 *
 * Accepts ONE client on the listening socket and enters the main event
 * loop: poll for client data, feed commands to the engine, flush engine
 * responses back to the client.
 *
 * Handles SIGTERM: stops the event loop cleanly (for test teardown).
 */

#include "uci_sim_tcp_server.h"
#include "uci_sim_packet.h"
#include "uci_sim_spec.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define ENGINE_TICK_MS 50  /* ms */

/* Global shutdown flag (set by SIGTERM handler) — per-thread in practice
   since tests create the child via fork(). */
static volatile sig_atomic_t g_shutdown = 0;

static void sigterm_handler(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

/* ─── helpers ───────────────】 */

static int send_to_client(int fd, const uci_sim_packet_t *pkt)
{
    uint8_t wire[UCI_SIM_MAX_PACKET];
    size_t w;
    if (uci_sim_packet_serialize(pkt, wire, sizeof(wire), &w) != 0)
        return -1;

    size_t total = 0;
    while (total < w) {
        ssize_t n = send(fd, (char*)wire + total, w - total, 0);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

static int flush_to_client(int fd, uci_sim_engine_t *engine)
{
    uci_sim_packet_t pkt;
    while (uci_sim_engine_dequeue_outbound_packet(engine, &pkt) == 0) {
        if (send_to_client(fd, &pkt) != 0)
            return -1;
    }
    return 0;
}

/*
 * Wire-format read: MT | PBF | GID (1 byte) + OID (1 byte) +
 * payload_len_LE (2 bytes) + payload (N bytes).
 *
 * Returns 0 = command ready | 1 = disconnect | -1 = error.
 */
static int read_command(int fd, uci_sim_packet_t *pkt)
{
    uint8_t hdr[4];
    {
        size_t off = 0;
        while (off < 4) {
            ssize_t n = recv(fd, hdr + off, sizeof(hdr) - off, MSG_WAITALL);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                return (n == 0) ? 1 : -1;
            }
            off += (size_t)n;
        }
    }

    pkt->mt      = (hdr[0] >> 5) & 0x03;
    pkt->pbf     = (hdr[0] >> 4) & 0x01;
    pkt->gid     = hdr[0] & 0x0F;
    pkt->oid     = hdr[1] & 0x3F;
    pkt->payload_len = (pkt->mt == UCI_MT_DATA)
        ? ((uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8))
        : (uint16_t)hdr[3];

    if (pkt->payload_len > 0) {
        size_t poff = 0;
        size_t max = pkt->payload_len;
        if (max > sizeof(pkt->payload)) max = sizeof(pkt->payload);
        while (poff < max) {
            ssize_t n = recv(fd, (char*)pkt->payload + poff, max - poff, 0);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                return (n == 0) ? 1 : -1;
            }
            poff += (size_t)n;
        }
        pkt->payload_len = (uint16_t)poff;
    }

    return 0;
}

/* ═══════ Public API ════
 */

int uci_sim_tcp_serve(const char *host, uint16_t port, uci_sim_engine_t *engine)
{
    int listen_fd, client_fd;
    int reuse = 1;
    struct sockaddr_in addr;

    /* Install SIGTERM handler for test teardown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    if (!engine) { errno = EINVAL; return -1; }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return -1; }

    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = (host && strcmp(host, "0.0.0.0") != 0)
                         ? inet_addr(host) : htonl(INADDR_ANY);
    if (host && strcmp(host, "0.0.0.0") != 0 && addr.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "[tcp] Invalid host: %s\n", host);
        close(listen_fd);
        return -1;
    }
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind"); close(listen_fd); return -1;
    }
    if (listen(listen_fd, 4) != 0) {
        perror("listen"); close(listen_fd); return -1;
    }

    fprintf(stderr, "[tcp] listening on %s:%u\n",
            host ? host : "0.0.0.0", port);

    /* Accept ONE client */
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept"); close(listen_fd); return -1;
    }

    fprintf(stderr, "[tcp] client connected\n");

    /* Event loop */
    for (;;) {
        if (g_shutdown) {
            fprintf(stderr, "[tcp] shutdown requested\n");
            break;
        }

        struct pollfd pfd[2];
        pfd[0].fd = client_fd;  pfd[0].events = POLLIN;
        pfd[1].fd = listen_fd;  pfd[1].events = POLLIN;

        int rc = poll(pfd, 2, ENGINE_TICK_MS);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (g_shutdown) break;

        if (pfd[1].revents & POLLIN) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int nf = accept(listen_fd, (struct sockaddr*)&caddr, &clen);
            if (nf >= 0) close(nf);
        }

        if (pfd[0].revents & POLLIN) {
            uci_sim_packet_t pkt;
            memset(&pkt, 0, sizeof(pkt));
            int cmd_rc = read_command(client_fd, &pkt);
            if (cmd_rc == 1)  break;  /* disconnect */
            if (cmd_rc == 0) {
                uci_sim_engine_submit_packet(engine, &pkt);
                if (flush_to_client(client_fd, engine) != 0)
                    break;
            } else {
                break;
            }
        }

        if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
            break;

        uci_sim_engine_tick(engine, ENGINE_TICK_MS);
        /* Flush any time-triggered outbound packets (range data, notifications) */
        if (flush_to_client(client_fd, engine) != 0)
            break;
    }

    close(client_fd);
    close(listen_fd);
    fprintf(stderr, "[tcp] disconnected\n");
    return 0;
}
