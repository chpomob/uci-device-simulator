/*
 * uci_sim_tcp_server.h
 *
 * TCP transport: listens on <host>:<port>, accepts one client, forwards
 * UCI wire bytes.  Uses the same binary protocol as a real UWB chardev.
 */
#ifndef UCI_SIM_TCP_SERVER_H
#define UCI_SIM_TCP_SERVER_H

#include "uci_sim_engine.h"

/**
 * Start listening and accepting ONE client, then run the event loop.
 *
 * - host:       bind address (e.g. "127.0.0.1" or "0.0.0.0")
 * - port:       port number
 * - engine:     simulator engine
 *
 * Returns 0 on normal client disconnect, -1 on fatal error.
 * This function DOES NOT RETURN until disconnect or error.
 */
int uci_sim_tcp_serve(const char *host, uint16_t port, uci_sim_engine_t *engine);

#endif /* UCI_SIM_TCP_SERVER_H */
