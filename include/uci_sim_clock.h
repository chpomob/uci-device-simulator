#ifndef UCI_SIM_CLOCK_H
#define UCI_SIM_CLOCK_H

#include <stdint.h>

typedef uint64_t uci_sim_time_ms_t;

typedef struct {
    uci_sim_time_ms_t (*now_ms)(void* context);
    void* context;
} uci_sim_clock_t;

uci_sim_time_ms_t uci_sim_clock_system_now_ms(void* context);
void uci_sim_clock_init_system(uci_sim_clock_t* clock);

#endif
