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

The first non-default scenario is `delayed_notifications`, which defers session-state notifications until the next command exchange. This gives clients a deterministic way to exercise lagged notification handling without changing the transport contract.

App-config state is now owned by the session model rather than encoded directly in handlers, so future protocol expansion can reuse the same storage path across scenarios.
Protocol-surface expansion should continue through this model-backed path and stay pinned by both simulator fixture tests and sibling-shell end-to-end integration, so Cherry/Qorvo semantic drift is caught outside the handlers.
Core device configuration now follows the same rule: the handlers only translate UCI TLVs, while persistent device config values live in the device model and are exposed through fixture-backed TCP interoperability tests.
Session-count behavior is also model-derived now, which keeps the simulator’s standard session bookkeeping observable without adding special-case state to the transport layer.
Ranging-query behavior now follows the same pattern: the session model owns fixed queryable fields such as `max_data_size` and `ranging_count`, while handlers only expose them through standard UCI responses.
