# Architecture

## Boundaries

- `spec`: constants and protocol interpretation aligned to Qorvo SDK definitions
- `core`: packet parsing and framing helpers
- `model`: mutable simulated device state
- `handlers`: command behavior that mutates the model and emits responses/notifications
- `scenario`: notification policy and deterministic behavior variants layered on top of the model
- `transport`: raw byte I/O adapters

## V1 Transport Strategy

TCP comes first because it is simple to automate, inspect, and test. The simulator core does not know about sockets; it only accepts parsed packets and produces response/notification packets.

## Evolution Plan

Future transports should only adapt raw bytes to `uci_sim_packet_t`. Session/ranging scenarios should live in dedicated model or scenario modules rather than transport or CLI code.

## Scenario Direction

The simulator now has an explicit scenario seam in the device model. The default scenario preserves current immediate notification behavior, and future variants should change notification timing or error injection without pushing test-only branches into the transport adapter.
