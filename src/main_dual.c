/* Ensure usleep(3) is declared */
#define _DEFAULT_SOURCE
/*
 * main_dual.c — dual-chardev+TCP simulator.
 *
 * Usage:
 *   uci-device-sim-dual chardev <scenario>
 *   uci-device-sim-dual tcp <host> <port> [scenario]
 */

#include <errno.h>      /* EAGAIN, errno, strerror */
#include <stdio.h>      /* fprintf */
#include <stdlib.h>     /* EXIT_SUCCESS */
#include <string.h>     /* strcmp */
#include <unistd.h>     /* usleep */

#include "uci_sim_chardev.h"
#include "uci_sim_engine.h"
#include "uci_sim_scenario.h"
#include "uci_sim_tcp_server.h"

#define TICK_MS 50   /* Engine tick granularity */

int main(int argc, char **argv)
{
    uci_sim_engine_t engine;
    uci_sim_chardev_t *chardev = NULL;
    uci_sim_scenario_kind_t scenario = UCI_SIM_SCENARIO_DEFAULT;
    int chardev_mode = 0;   /* 1 = chardev mode, 0 = tcp mode */
    int rc;

    /* ---- Parse args ---- */
    if (argc < 3) {
        fprintf(stderr,
                "Usage:\n"
                "  %s chardev <scenario>              # PTY chardev mode\n"
                "  %s tcp <host> <port> [scenario]     # TCP mode\n",
                argv[0], argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "chardev") == 0) {
        chardev_mode = 1;
        if (uci_sim_scenario_parse(argv[2], &scenario) != 0) {
            fprintf(stderr, "Invalid scenario: %s\n", argv[2]);
            return 1;
        }
    } else if (strcmp(argv[1], "tcp") == 0) {
        chardev_mode = 0;
        if (argc >= 5 && uci_sim_scenario_parse(argv[4], &scenario) != 0) {
            fprintf(stderr, "Invalid scenario: %s\n", argv[4]);
            return 1;
        }
    } else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        return 1;
    }

    /* ---- Engine ---- */
    uci_sim_engine_init_with_scenario(&engine, scenario);
    fprintf(stderr, "[eng] scenario: %s\n", uci_sim_scenario_name(scenario));

    /* ---- Transport ---- */
    if (chardev_mode) {
        const char *pty_path = NULL;
        chardev = uci_sim_chardev_start(&engine, &pty_path);
        if (!chardev) {
            fprintf(stderr, "[chardev] failed to create chardev\n");
            return 1;
        }
        fprintf(stderr,
                "\n═══════════════════════════════════════════════\n"
                "  Chardev slave path: %s\n"
                "  Connect:  shell -m chardev:%s\n"
                "═══════════════════════════════════════════════\n\n",
                pty_path, pty_path);
    } else {
        const char *host = "127.0.0.1";
        uint16_t port = 9000;
        if (argc >= 3) host = argv[2];
        if (argc >= 4) port = (uint16_t)atoi(argv[3]);
        rc = uci_sim_tcp_serve(host, port, &engine);
        if (rc != 0) {
            fprintf(stderr, "[tcp] failed on %s:%u\n", host, port);
            return 1;
        }
        return 0;
    }

    /* ---- Event loop (chardev mode) ---- */
    for (;;) {
        if (chardev) {
            rc = uci_sim_chardev_process_input(chardev);
            if (rc < 0 && errno != EAGAIN) break;
        }

        /* Tick engine */
        rc = uci_sim_engine_tick(&engine, TICK_MS);
        if (rc != 0) break;

        usleep(TICK_MS * 1000);
    }

    uci_sim_chardev_destroy(chardev);
    return 0;
}
