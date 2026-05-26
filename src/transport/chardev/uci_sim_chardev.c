/*
 * uci_sim_chardev.c
 *
 * Implements the chardev layer.  A PTY pair is created internally:
 *
 *   Master fd   — operated on by THIS library (read engine input, write engine output)
 *   Slave fd     — presented as a /dev/pts/N path (the "hardware" for the tester)
 *
 * The PTY kernel pipe transparently routes:
 *   Host write(slave → master) → chardev reads → engine.submit_packet()
 *                               ← engine-enqueued packets ← wire_bytes(master ← engine)
 *
 * Wire format (bytes on the PTY pipe):
 *   [0]  MT | PBF | GID    (bit fields, big-endian)
 *   [1]  OID  | reserved
 *   [2]  payload_len (LE u16 for DATA, reserved for non-DATA)
 *   [3]  (for non-DATA: payload_len in byte [3] only)
 *   [4..] payload
 */

#define _POSIX_C_SOURCE 200809L

#include "uci_sim_chardev.h"
#include "uci_sim_engine.h"
#include "uci_sim_packet.h"
#include "uci_sim_spec.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>       /* PATH_MAX */
#include <poll.h>
#include <pty.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>    /* ssize_t */
#include <sys/stat.h>
#include <unistd.h>

/* ════════════════ Internal stream buffer ════════════════
 *
 * Raw PTY reads don't align on packet boundaries.  We accumulate
 * bytes in a stream buffer until a complete packet (header + payload)
 * is ready, pass it to the engine, then discard consumed bytes and
 * keep any partial tail.
 */

struct uci_sim_chardev {
    int                m_fd;      /* PTY master file descriptor */
    uci_sim_engine_t  *engine;    /* non-owning back-pointer to engine */
    uint8_t           *stream;    /* packet accumulator */
    size_t             slen;      /* valid bytes in stream[0..slen-1] */
    char              *pty_path;  /* owned copy of the slave PTY path */
};

/* ─── stream_feed_to_engine ───────────────────────────────────────
 * From stream[0..slen-1], parse as many complete packets as possible,
 * submit each to the engine via uci_sim_engine_submit_packet(),
 * and compact the remaining tail to the front of the buffer.
 *
 * Payload length is interpreted according to the message type:
 *  - UCI_MT_DATA: full LE u16 from bytes [2..3]
 *  - all others:  byte [3] only (byte [2] is reserved)
 *
 * Returns 0 on success, -1 on error (malformed data or engine error).
 */
static int stream_feed_to_engine(uci_sim_chardev_t *dev)
{
    size_t consumed = 0;  /* total bytes consumed this round */

    for (;;) {
        /* Maintain invariant: consumed <= dev->slen. A violation means
         * the loop body advanced past the buffer end — treat as a logic bug. */
        if (consumed > dev->slen)
            return -1;
        size_t avail = dev->slen - consumed;
        if (avail < UCI_SIM_HEADER_SIZE)
            break;

        /* Payload length: delegate to packet_parse's interpretation.
         * For non-DATA messages (MT != 3), byte[2] is reserved and
         * payload length is in byte[3] only. For DATA messages (MT=3),
         * bytes [2..3] are a full LE u16. */
        uint8_t mt = (uint8_t)(*(dev->stream + consumed) >> 5) & 0x03;
        uint16_t plen;
        if (mt == UCI_MT_DATA) {
            plen = (uint16_t)((uint8_t)(*(dev->stream + consumed + 2)))
                 | ((uint16_t)((uint8_t)(*(dev->stream + consumed + 3))) << 8);
        } else {
            plen = (uint8_t)(*(dev->stream + consumed + 3));
        }

        /* Sanity: UCI payloads must be <= 255 bytes */
        if (plen > 255) {
            fprintf(stderr, "[chardev reject: payload_len=%u\n", plen);
            return -1;
        }

        size_t pkt_total = UCI_SIM_HEADER_SIZE + (size_t)plen;

        if (avail < pkt_total)
            break;  /* partial packet — wait for more data */

        /* We have a complete packet at stream[consumed .. consumed+pkt_total-1] */
        uci_sim_packet_t pkt;
        if (uci_sim_packet_parse(dev->stream + consumed, pkt_total, &pkt) != 0)
            return -1;

        /* Submit to engine */
        if (uci_sim_engine_submit_packet(dev->engine, &pkt) != 0)
            return -1;

        consumed += pkt_total;
    }

    /* Compact the remaining tail to the front of the buffer */
    if (consumed > 0 && consumed < dev->slen) {
        size_t remaining = dev->slen - consumed;
        memmove(dev->stream, dev->stream + consumed, remaining);
    }
    dev->slen -= consumed;
    return 0;
}

/* ─── master_flush ───────────────────────────────────────────────────
 * Serialize every packet in the engine's outbound queue and write it
 * (as raw wire bytes) onto the PTY master file descriptor.
 *
 * Returns -1 if serialization fails (indicates internal engine corruption).
 */
static int master_flush(uci_sim_chardev_t *dev)
{
    uci_sim_packet_t pkt;

    while (uci_sim_engine_dequeue_outbound_packet(dev->engine, &pkt) == 0) {
        uint8_t wire[UCI_SIM_MAX_PACKET];
        size_t written;
        if (uci_sim_packet_serialize(&pkt, wire, sizeof(wire), &written) != 0) {
            fprintf(stderr, "[chardev] serialize failed for outbound packet\n");
            return -1;
        }

        /* Write the whole serialized packet to the PTY master */
        const uint8_t *p  = wire;
        size_t left       = written;
        while (left > 0) {
            ssize_t n;
            do { n = write(dev->m_fd, p, left); } while (n < 0 && errno == EINTR);
            if (n <= 0) return -1;
            p   += (size_t)n;
            left -= (size_t)n;
        }
    }
    return 0;
}

/* ════════════════ Public API ════════════════ */

uci_sim_chardev_t *uci_sim_chardev_start(uci_sim_engine_t *engine,
                                         const char **out_pty_path)
{
    if (!engine) { errno = EINVAL; return NULL; }

    int m_fd = -1, s_fd = -1;
    uci_sim_chardev_t *dev = NULL;
    char sl_name[PATH_MAX];

    /* Open PTY pair: m_fd = master, s_fd = slave */
    if (openpty(&m_fd, &s_fd, sl_name, NULL, NULL) != 0) {
        fprintf(stderr, "[chardev openpty failed: %s\n", strerror(errno));
        return NULL;
    }

    /* The master fd is read non-blockingly in process_input(); set the flag
     * here so read() returns EAGAIN when no host data is queued. Setting
     * O_NONBLOCK on s_fd would not affect the host's later open() of the
     * slave path — each open() produces its own open file description. */
    int fl = fcntl(m_fd, F_GETFL);
    if (fl < 0) goto fail;
    if (fcntl(m_fd, F_SETFL, fl | O_NONBLOCK) != 0) goto fail;

    /* Make /dev/pts/N world-readable/writable */
    chmod(sl_name, 0666);

    /* Allocate context */
    dev = calloc(1, sizeof(*dev));
    if (!dev) goto fail;

    dev->m_fd      = m_fd;
    dev->engine    = engine;
    dev->stream    = NULL;
    dev->slen      = 0;
    dev->pty_path  = strdup(sl_name);
    if (!dev->pty_path) goto fail;

    /*
     * Close our copy of the slave fd.  The slave-side path (/dev/pts/N)
     * is still open in the filesystem — any process that opens() it gets
     * a valid FD.  This avoids "already open" races.
     */
    close(s_fd);
    s_fd = -1;

    if (out_pty_path) *out_pty_path = dev->pty_path;

    fprintf(stderr, "[chardev] ready  master_fd=%d  slave=%s\n",
            m_fd, dev->pty_path);
    return dev;

fail:
    if (dev) { free(dev->pty_path); free(dev); }
    if (m_fd >= 0) close(m_fd);
    if (s_fd >= 0) close(s_fd);
    return NULL;
}

void uci_sim_chardev_destroy(uci_sim_chardev_t *dev)
{
    if (!dev) return;
    if (dev->m_fd >= 0) close(dev->m_fd);
    free(dev->stream);
    free(dev->pty_path);
    free(dev);
}

int uci_sim_chardev_process_input(uci_sim_chardev_t *dev)
{
    if (!dev || dev->m_fd < 0) { errno = EBADF; return -1; }

    /* 1. Read all available bytes from PTY master */
    uint8_t buf[UCI_SIM_MAX_PACKET];
    ssize_t n;

    do {
        n = read(dev->m_fd, buf, sizeof(buf));
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; /* no data */
        if (errno == EIO) return 0; /* PTY master: no slave connected yet */
        return -1;  /* real error */
    }
    if (n == 0) return -1;  /* master closed (EOF) */

    /* 2. Feed stream to engine */
    /*
     * We need to accumulate into the existing stream buffer.
     * First, grow it if necessary.
     */
    size_t need = dev->slen + (size_t)n;
    if (need > UCI_SIM_MAX_PACKET) {
        /* Safety: drop excess, keep stream[0..UCI_SIM_MAX_PACKET-1] */
        need = UCI_SIM_MAX_PACKET;
        memmove(buf, buf + n - (UCI_SIM_MAX_PACKET - dev->slen),
                UCI_SIM_MAX_PACKET - dev->slen);
    }

    if (need > dev->slen) {
        if (!dev->stream || need > (size_t)UCI_SIM_MAX_PACKET) {
            uint8_t *nv = realloc(dev->stream, UCI_SIM_MAX_PACKET);
            if (!nv) return -1;
            dev->stream = nv;
        }
        memcpy(dev->stream + dev->slen, buf, n);
        dev->slen  = need < UCI_SIM_MAX_PACKET ? need : UCI_SIM_MAX_PACKET;
    }

    /* 3. Feed complete packets from stream → engine */
    if (stream_feed_to_engine(dev) != 0)
        return -1;

    /* 4. Flush engine-enqueued responses → master */
    return master_flush(dev);
}

ssize_t uci_sim_chardev_inject(uci_sim_chardev_t *dev,
                               const uint8_t *data, size_t length)
{
    if (!dev || !data || length == 0 || dev->m_fd < 0) {
        errno = EBADF;
        return -1;
    }

    const uint8_t *p  = data;
    size_t left       = length;
    while (left > 0) {
        ssize_t n;
        do { n = write(dev->m_fd, p, left); } while (n < 0 && errno == EINTR);
        if (n <= 0) return -1;
        p   += (size_t)n;
        left -= (size_t)n;
    }
    return (ssize_t)length;
}
