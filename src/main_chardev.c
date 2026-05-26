/* Ensure usleep(3) is declared */
#define _DEFAULT_SOURCE
/*
 * main_chardev.c — UWB hardware simulator via PTY chardev transport.
 *
 * Usage:    uci-device-sim-chardev <default|delayed|stream>
 *
 * Creates a PTY /dev/pts/N, prints the slave path to stderr.
 * Connect any host tester at -m chardev:/dev/pts/N
 */

#include <errno.h>      /* EAGAIN, errno    */
#include <stdio.h>      /* fprintf        */
#include <stdlib.h>     /* EXIT_SUCCESS   */
#include <unistd.h>     /* usleep         */

#include "uci_sim_chardev.h"
#include "uci_sim_engine.h"
#include "uci_sim_scenario.h"

#define TICK_MS 50   /* Engine tick granularity */

int main(int argc, char **argv)
{
    uci_sim_engine_t engine;
    uci_sim_chardev_t *chardev = NULL;
    const char *pty_path = NULL;
    int rc;

    /* ---- Scenario ---- */
    uci_sim_scenario_kind_t scenario = UCI_SIM_SCENARIO_DEFAULT;
    if (argc >= 2) {
        if (uci_sim_scenario_parse(argv[1], &scenario) != 0) {
            fprintf(stderr, "Usage: %s <default|delayed|stream>\n", argv[0]);
            return 1;
        }
    }

    uci_sim_engine_init_with_scenario(&engine, scenario);

    /* ---- Chardev ---- */
    chardev = uci_sim_chardev_start(&engine, &pty_path);
    if (!chardev) {
        fprintf(stderr, "Failed to create chardev\n");
        return 1;
    }

    fprintf(stderr,
            "\n════════════════════════════════════════\n"
            "  Chardev slave path: %s\n"
            "  Connect:  shell -m chardev:%s\n"
            "════════════════════════════════════════\n\n",
            pty_path, pty_path);

    /* ---- Event loop ---- */
    for (;;) {
        /* Process I/O (host→engine on master → flush output to master) */
        rc = uci_sim_chardev_process_input(chardev);
        if (rc < 0) {
            /* Check if it was just "no data available"** */
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            break;  /* master closed or fatal error */
        }

        /* Tick the engine (timers, state machines, etc.) */
        rc = uci_sim_engine_tick(&engine, TICK_MS);
        if (rc != 0) break;

        usleep(TICK_MS * 1000);
    }

    uci_sim_chardev_destroy(chardev);
    return 0;
}
