/*
 * uci_sim_chardev.h
 *
 * Chardev simulation layer: presents a standard UNIX character device (PTY)
 * that bridges host-process I/O to the simulator engine.
 *
 * Real Qorvo UWB hardware is accessed via a character device: open() → write()
 * → read().  This simulator creates a PTY pair so the same byte stream
 * protocol works verbatim.
 *
 *   ┌─────── host tester proc ─────────┐  /dev/pts/N ┌─────── simulator proc ───────┐
 *   │  open(slave_path)              ══════════════════ master_fd                   │
 *   │  write(cmd_packet)  ─────────▶                  read()     engine              │
 *   │  read(response_packet) ◀──────  PTY kernel pipe  process_input() ─▶ submitpkt │
 *   │                                        flush_output() ◀── outbound packets      │
 *   └───────────────────────────────────┘             └─────────────────────────────┘
 *
 * This gives the host-side tester hardware-equivalent I/O semantics:
 *   - poll()/select() on slave fd
 *   - raw binary reads
 *   - same wire format (MT + PBF + GID + OID + lenLE + payload)
 */
#ifndef UCI_SIM_CHARDEV_H
#define UCI_SIM_CHARDEV_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */
#include "uci_sim_engine.h"

/** Opaque chardev context. */
typedef struct uci_sim_chardev uci_sim_chardev_t;

/**
 * Create a PTY pair and bind the master to the given engine.
 *
 * Returns opaque chardev handle (non-NULL on success, NULL on failure,
 * sets errno).  *out_pty_path (if non-NULL) receives the slave side path.
 *
 * The returned handle MUST be freed with uci_sim_chardev_destroy().
 */
uci_sim_chardev_t *uci_sim_chardev_start(uci_sim_engine_t *engine,
                                         const char **out_pty_path);

/** Destroy the chardev, closing the PTY master and freeing resources. */
void uci_sim_chardev_destroy(uci_sim_chardev_t *dev);

/**
 * Process all pending host → engine data.
 *
 * 1. Reads raw bytes from the PTY master.
 * 2. Assembles complete UCI packets (streaming over multiple reads).
 * 3. Parses each packet and submits to the engine via
 *    uci_sim_engine_submit_packet().
 * 4. Serializes every engine-enqueued outbound packet and writes it
 *    back onto the PTY master (← becomes visible as slave-side read()).
 *
 * Returns 0 on success, -1 on error (e.g. master closed).
 */
int uci_sim_chardev_process_input(uci_sim_chardev_t *dev);

/**
 * Inject raw wire-format data onto the PTY master side (simulates the
 * hardware device initiating a message to the host).
 *
 * Returns number of bytes written on success, -1 on error.
 */
ssize_t uci_sim_chardev_inject(uci_sim_chardev_t *dev,
                               const uint8_t *data, size_t length);

#endif /* UCI_SIM_CHARDEV_H */
