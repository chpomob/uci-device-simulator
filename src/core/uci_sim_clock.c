#define _POSIX_C_SOURCE 200809L

#include "uci_sim_clock.h"

#include <time.h>

uci_sim_time_ms_t uci_sim_clock_system_now_ms(void* context) {
    struct timespec ts;

    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ((uci_sim_time_ms_t)ts.tv_sec * 1000U) +
           ((uci_sim_time_ms_t)ts.tv_nsec / 1000000U);
}

void uci_sim_clock_init_system(uci_sim_clock_t* clock) {
    if (!clock) {
        return;
    }

    clock->now_ms = uci_sim_clock_system_now_ms;
    clock->context = NULL;
}
