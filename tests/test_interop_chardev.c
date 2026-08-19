#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "uci_sim_chardev.h"
#include "uci_sim_engine.h"
#include "uci_sim_packet.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int g_failed = 0;
static int g_passed = 0;

#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define ASSERT_EQ_INT(exp, act, msg) do { if ((exp) != (act)) { printf("FAIL: %s\n", msg); g_failed++; return; } } while (0)
#define PASS() do { g_passed++; } while (0)

/* ─── Polling read helper (known-working pattern) ─────────── */
static ssize_t read_poll(int fd, void* buf, size_t n) {
    struct pollfd pfd = {fd, POLLIN, 0};
    if (poll(&pfd, 1, 2000) <= 0) return -1;
    return read(fd, buf, n);
}

static int read_packet_poll(int fd, uint8_t* buf, size_t cap, size_t* len) {
    uint8_t hdr[4];
    if (read_poll(fd, hdr, 4) != 4) return -1;

    size_t plen = (((hdr[0] >> 5) & 0x03) == UCI_MT_DATA)
        ? ((size_t)hdr[2] | ((size_t)hdr[3] << 8))
        : (size_t)hdr[3];

    if (cap < 4 + plen) return -1;
    memcpy(buf, hdr, 4);
    if (plen > 0 && read_poll(fd, buf + 4, plen) != (ssize_t)plen) return -1;
    *len = 4 + plen;
    return 0;
}

/* ─── Chardev test context ────────────────────────────────── */
typedef struct {
    uci_sim_engine_t  engine;
    uci_sim_chardev_t *chardev;
    const char        *pty_path;
    int               slave_fd;
} ctx_t;

static int ctx_init(ctx_t *c, uci_sim_scenario_kind_t sc) {
    memset(c, 0, sizeof(*c));
    c->slave_fd = -1;
    uci_sim_engine_init_with_scenario(&c->engine, sc);
    c->chardev = uci_sim_chardev_start(&c->engine, &c->pty_path);
    if (!c->chardev) return -1;
    c->slave_fd = open(c->pty_path, O_RDWR | O_NONBLOCK);
    if (c->slave_fd < 0) { uci_sim_chardev_destroy(c->chardev); return -1; }
    struct termios t;
    tcgetattr(c->slave_fd, &t);
    cfmakeraw(&t);
    tcsetattr(c->slave_fd, TCSANOW, &t);
    return 0;
}

static void ctx_done(ctx_t *c) {
    if (c->slave_fd >= 0) { close(c->slave_fd); c->slave_fd = -1; }
    if (c->chardev) { uci_sim_chardev_destroy(c->chardev); c->chardev = NULL; }
}

static int process(ctx_t *c) {
    return uci_sim_chardev_process_input(c->chardev);
}

/* ─── Tests ───────────────────────────────────────────────── */

static void test_device_info(void) {
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");

    uint8_t cmd[] = {0x20, 0x02, 0x00, 0x00};
    ASSERT_TRUE(write(c.slave_fd, cmd, 4) == 4, "write cmd");
    ASSERT_TRUE(process(&c) == 0, "process");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse");
    ASSERT_EQ_INT(UCI_GID_CORE, p.gid, "gid");
    ASSERT_EQ_INT(UCI_CORE_DEVICE_INFO, p.oid, "oid");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "status");

    ctx_done(&c);
    PASS();
}

static void test_device_reset(void) {
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");

    uint8_t cmd[] = {0x20, 0x00, 0x00, 0x01, 0x01};
    ASSERT_TRUE(write(c.slave_fd, cmd, 5) == 5, "write cmd");
    ASSERT_TRUE(process(&c) == 0, "process");

    /* Response */
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read rsp");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse rsp");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "rsp status");

    /* Notification */
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read ntf");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse ntf");
    ASSERT_EQ_INT(UCI_DEVICE_STATE_READY, p.payload[0], "ntf status");

    ctx_done(&c);
    PASS();
}

static void test_full_flow(void) {
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;
    uint8_t cmd[16];

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");

    /* RESET */
    memcpy(cmd, (uint8_t[]){0x20, 0x00, 0x00, 0x01, 0x01}, 5);
    ASSERT_TRUE(write(c.slave_fd, cmd, 5) == 5, "write reset");
    ASSERT_TRUE(process(&c) == 0, "process reset");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read reset rsp");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse reset rsp");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "reset status");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read reset ntf");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse reset ntf");
    ASSERT_EQ_INT(UCI_DEVICE_STATE_READY, p.payload[0], "ready ntf");

    /* DEVICE_INFO */
    memcpy(cmd, (uint8_t[]){0x20, 0x02, 0x00, 0x00}, 4);
    ASSERT_TRUE(write(c.slave_fd, cmd, 4) == 4, "write dev_info");
    ASSERT_TRUE(process(&c) == 0, "process dev_info");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read dev_info");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse dev_info");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "dev_info status");

    /* GET_CAPS_INFO */
    memcpy(cmd, (uint8_t[]){0x20, 0x03, 0x00, 0x00}, 4);
    ASSERT_TRUE(write(c.slave_fd, cmd, 4) == 4, "write caps");
    ASSERT_TRUE(process(&c) == 0, "process caps");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read caps");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse caps");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "caps status");

    /* SESSION_INIT */
    memcpy(cmd, (uint8_t[]){0x21, 0x00, 0x00, 0x05, 0x78, 0x56, 0x34, 0x12, UCI_SESSION_TYPE_RANGING}, 9);
    ASSERT_TRUE(write(c.slave_fd, cmd, 9) == 9, "write sess_init");
    ASSERT_TRUE(process(&c) == 0, "process sess_init");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read sess_init rsp");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse sess_init rsp");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "sess_init status");

    /* SESSION_STATUS_NTF emitted by the SESSION_INIT handler */
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0,
                "read sess_init ntf");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse sess_init ntf");
    ASSERT_EQ_INT(UCI_MT_NOTIFICATION, p.mt, "ntf mt");
    ASSERT_EQ_INT(UCI_GID_SESSION_CONFIG, p.gid, "ntf gid");
    ASSERT_EQ_INT(UCI_SESSION_STATUS_NTF, p.oid, "ntf oid");

    ctx_done(&c);
    PASS();
}

/* ─── Lifetime test: PTY path survives stack teardown ────── */
static void test_pty_path_lifetime(void) {
    uci_sim_engine_t engine;
    uci_sim_chardev_t *chardev;
    const char *pty_path = NULL;

    uci_sim_engine_init_with_scenario(&engine, UCI_SIM_SCENARIO_DEFAULT);
    chardev = uci_sim_chardev_start(&engine, &pty_path);
    ASSERT_TRUE(chardev != NULL, "start");
    ASSERT_TRUE(pty_path != NULL, "path returned");

    /* Clobber a large stack region to ensure any dangling stack pointer
     * would now read garbage. volatile prevents compiler elision. */
    volatile char stack_filler[PATH_MAX * 2];
    for (size_t i = 0; i < sizeof(stack_filler); ++i) stack_filler[i] = (char)0xAA;
    (void)stack_filler;

    int fd = open(pty_path, O_RDWR);
    ASSERT_TRUE(fd >= 0, "open path after stack perturbation");
    close(fd);

    uci_sim_chardev_destroy(chardev);
    PASS();
}

/* ─── Header fragmentation: bytes arrive one at a time ───── */
static void test_fragmented_header(void) {
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");

    /* DEVICE_INFO written byte-by-byte, process() between each */
    uint8_t cmd[] = {0x20, 0x02, 0x00, 0x00};
    for (size_t i = 0; i < sizeof(cmd); ++i) {
        ASSERT_TRUE(write(c.slave_fd, &cmd[i], 1) == 1, "write byte");
        ASSERT_TRUE(process(&c) == 0, "process partial");
    }
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "status");

    ctx_done(&c);
    PASS();
}

/* ─── Non-DATA byte[2] reserved: must be ignored ──────────── */
static void test_non_data_byte2_reserved(void) {
    /* For non-DATA packets, byte[2] is reserved and MUST be ignored:
     * the payload length is in byte[3] alone. We send byte[2]=0xFF (junk)
     * with a valid 1-byte payload and expect normal behavior. */
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");
    uint8_t cmd[] = {0x20, 0x00, 0xFF, 0x01, 0x01};
    ASSERT_TRUE(write(c.slave_fd, cmd, 5) == 5, "write cmd");
    ASSERT_TRUE(process(&c) == 0, "process");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read rsp");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse rsp");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "rsp status");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read ntf");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse ntf");
    ASSERT_EQ_INT(UCI_DEVICE_STATE_READY, p.payload[0], "ntf state");
    ctx_done(&c);
    PASS();
}

static void test_unknown_oid(void) {
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");

    uint8_t cmd[] = {0x20, 0x3F, 0x00, 0x00};
    ASSERT_TRUE(write(c.slave_fd, cmd, 4) == 4, "write");
    ASSERT_TRUE(process(&c) == 0, "process");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse");
    ASSERT_EQ_INT(UCI_GID_CORE, p.gid, "gid");
    ASSERT_EQ_INT(UCI_STATUS_UNKNOWN_OID, p.payload[0], "status");

    ctx_done(&c);
    PASS();
}

/* ─── Drain helper: discard everything currently queued for read on
 * fd, so stale packets from a prior step don't shadow a later read. ── */
static void drain_all_pending(int fd) {
    uint8_t scratch[256];
    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 50) <= 0) break;
        ssize_t n = read(fd, scratch, sizeof(scratch));
        if (n <= 0) break;
    }
}

/* ─── Stream-accumulator overflow: the clamp must never write past
 * dev->stream's UCI_SIM_MAX_PACKET bound, and the accumulator must
 * remain correctly parseable afterward, not merely non-crashing
 * (R1 / AC1, AC2) ─────────────────────────────────────────────── */
static void test_stream_overflow_clamped(void) {
    ctx_t c;
    uint8_t buf[UCI_SIM_MAX_PACKET];
    uci_sim_packet_t p;
    size_t plen = 0;
    uint8_t chunk1[8];
    static uint8_t chunk2[UCI_SIM_MAX_PACKET];
    uint8_t reset_cmd[] = {0x20, 0x00, 0x00, 0x01, 0x01};
    size_t off;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");

    /* 1. Leave a partial packet buffered: header claims an 8-byte
     * payload (unknown OID 0x3F -> no device-state side effects) but
     * only 4 garbage payload bytes actually arrive, so
     * stream_feed_to_engine() must wait for more data. */
    chunk1[0] = 0x20; chunk1[1] = 0x3F; chunk1[2] = 0x00; chunk1[3] = 0x08;
    memset(chunk1 + 4, 0xAA, 4);
    ASSERT_TRUE(write(c.slave_fd, chunk1, sizeof(chunk1)) == (ssize_t)sizeof(chunk1),
                "write leading partial packet");
    ASSERT_TRUE(process(&c) == 0, "process partial packet");

    /* 2. Send a second chunk sized at UCI_SIM_MAX_PACKET -- the
     * largest a single read() inside process_input() can return --
     * so old_slen(8) + n(UCI_SIM_MAX_PACKET) exceeds UCI_SIM_MAX_PACKET
     * and exercises the clamp. Its first 4 bytes complete packet #1's
     * claimed 8-byte payload; the rest is filled with as many more
     * well-formed unknown-OID commands as fit (254 bytes each:
     * 4-byte header + 250-byte payload), derived from sizeof(chunk2)
     * rather than a fixed count so the layout keeps exercising the
     * clamp regardless of how UCI_SIM_MAX_PACKET is defined. Any
     * leftover bytes that don't form a full 254-byte command are
     * zeroed and dropped by the clamp itself, never parsed. */
    memset(chunk2, 0xBB, 4);
    off = 4;
    while (off + 254 <= sizeof(chunk2)) {
        chunk2[off + 0] = 0x20;
        chunk2[off + 1] = 0x3F;
        chunk2[off + 2] = 0x00;
        chunk2[off + 3] = 250;
        memset(chunk2 + off + 4, 0xCC, 250);
        off += 254;
    }
    memset(chunk2 + off, 0x00, sizeof(chunk2) - off);
    ASSERT_TRUE(write(c.slave_fd, chunk2, sizeof(chunk2)) == (ssize_t)sizeof(chunk2),
                "write overflow-inducing chunk");
    ASSERT_TRUE(process(&c) == 0, "process overflow chunk without corruption");

    /* Drain the five UNKNOWN_OID responses generated above so they
     * don't shadow the probe response read below. */
    drain_all_pending(c.slave_fd);

    /* 3. Prove dev->slen ended at a sane, in-bounds value (not
     * corrupted or unbounded) by sending one more well-formed
     * command and requiring a byte-exact correct response -- a
     * garbled/missing response here would indicate the accumulator's
     * notion of "bytes already buffered" survived the overflow in a
     * corrupted state. */
    ASSERT_TRUE(write(c.slave_fd, reset_cmd, sizeof(reset_cmd)) == (ssize_t)sizeof(reset_cmd),
                "write probe reset");
    ASSERT_TRUE(process(&c) == 0, "process probe reset");
    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read probe rsp");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse probe rsp");
    ASSERT_EQ_INT(UCI_MT_RESPONSE, p.mt, "probe rsp mt");
    ASSERT_EQ_INT(UCI_GID_CORE, p.gid, "probe rsp gid");
    ASSERT_EQ_INT(UCI_CORE_DEVICE_RESET, p.oid, "probe rsp oid");
    ASSERT_EQ_INT(UCI_STATUS_OK, p.payload[0], "probe rsp status");
    ASSERT_EQ_INT(5, (int)plen, "probe rsp total length");

    ASSERT_TRUE(read_packet_poll(c.slave_fd, buf, sizeof(buf), &plen) == 0, "read probe ntf");
    ASSERT_TRUE(uci_sim_packet_parse(buf, plen, &p) == 0, "parse probe ntf");
    ASSERT_EQ_INT(UCI_DEVICE_STATE_READY, p.payload[0], "probe ntf state");

    ctx_done(&c);
    PASS();
}

/* ─── PTY slave device node must not be world-readable/writable
 * (R2 / AC3) ──────────────────────────────────────────────────── */
static void test_pty_slave_not_world_accessible(void) {
    ctx_t c;
    struct stat st;

    ASSERT_TRUE(ctx_init(&c, UCI_SIM_SCENARIO_DEFAULT) == 0, "init");
    ASSERT_TRUE(stat(c.pty_path, &st) == 0, "stat pty path");
    ASSERT_TRUE((st.st_mode & (S_IROTH | S_IWOTH)) == 0, "pty slave not world-accessible");

    ctx_done(&c);
    PASS();
}

int main(void) {
    printf("═══ Chardev Tests ═══\n\n");
    test_device_info();
    test_device_reset();
    test_unknown_oid();
    test_full_flow();
    test_pty_path_lifetime();
    test_fragmented_header();
    test_non_data_byte2_reserved();
    test_stream_overflow_clamped();
    test_pty_slave_not_world_accessible();
    printf("\nPassed: %d\nFailed: %d\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
