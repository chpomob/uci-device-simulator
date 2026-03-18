#include "uci_sim_engine.h"
#include "uci_sim_scenario.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int uci_sim_tcp_serve(const char* host, uint16_t port, uci_sim_engine_t* engine);

int main(int argc, char** argv) {
    uci_sim_engine_t engine;
    const char* host = "127.0.0.1";
    uint16_t port = 9000;
    uci_sim_scenario_kind_t scenario = UCI_SIM_SCENARIO_DEFAULT;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = (uint16_t)strtoul(argv[2], NULL, 10);
    }
    if (argc >= 4 && uci_sim_scenario_parse(argv[3], &scenario) != 0) {
        fprintf(stderr, "Unknown scenario: %s\n", argv[3]);
        return 1;
    }

    uci_sim_engine_init_with_scenario(&engine, scenario);
    printf("Scenario: %s\n", uci_sim_scenario_name(scenario));
    return uci_sim_tcp_serve(host, port, &engine);
}
