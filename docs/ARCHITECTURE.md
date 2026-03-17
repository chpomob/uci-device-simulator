# Architecture

## Boundaries

- `spec`: constants and protocol interpretation aligned to Qorvo SDK definitions
- `core`: packet parsing and framing helpers
- `model`: mutable simulated device state
- `handlers`: command behavior that mutates the model and emits responses/notifications
- `transport`: raw byte I/O adapters

## V1 Transport Strategy

TCP comes first because it is simple to automate, inspect, and test. The simulator core does not know about sockets; it only accepts parsed packets and produces response/notification packets.

## Evolution Plan

Future transports should only adapt raw bytes to `uci_sim_packet_t`. Session/ranging scenarios should live in dedicated model or scenario modules rather than transport or CLI code.
