# Architecture

## Boundaries

- `spec`: constants and protocol interpretation aligned to Qorvo SDK definitions
- `core`: packet parsing, framing helpers, and the device engine / outbound queue
- `model`: mutable simulated device state
- `handlers`: command behavior that mutates the model and emits responses/notifications
- `scenario`: notification policy and deterministic behavior variants layered on top of the model
- `transport`: raw byte I/O adapters

## V1 Transport Strategy

TCP comes first because it is simple to automate, inspect, and test. The
transport now talks to a device engine, not directly to the handlers. Host
packets are submitted into the engine, the engine advances its internal clock,
and outbound packets are drained from the engine queue.

## Evolution Plan

Future transports should only adapt raw bytes to `uci_sim_packet_t` and the
engine API. Session/ranging scenarios should live in dedicated model or
scenario modules rather than transport or CLI code.

## Scenario Direction

The simulator now has an explicit scenario seam through `uci_sim_scenario.*`
and a small internal scheduled-event queue in the device core. The default
scenario preserves current immediate notification behavior, and future variants
should change notification timing or error injection by scheduling or canceling
events through scenario hooks instead of pushing test-only branches into the
transport adapter or the core device model.

The first non-default scenario is `delayed_notifications`, which defers
session-state notifications until the next command exchange. This gives
clients a deterministic way to exercise lagged notification handling without
changing the transport contract.

App-config state is now owned by the session model rather than encoded directly in handlers, so future protocol expansion can reuse the same storage path across scenarios.
Protocol-surface expansion should continue through this model-backed path and stay pinned by both simulator fixture tests and sibling-shell end-to-end integration, so Cherry/Qorvo semantic drift is caught outside the handlers.
Core device configuration now follows the same rule: the handlers only translate UCI TLVs, while persistent device config values live in the device model and are exposed through fixture-backed TCP interoperability tests.
Session-count behavior is also model-derived now, which keeps the simulator’s standard session bookkeeping observable without adding special-case state to the transport layer.
Ranging-query behavior now follows the same pattern: the session model owns fixed queryable fields such as `max_data_size` and `ranging_count`, while handlers only expose them through standard UCI responses.
The first notification-centric scenario is `ranging_stream`. It keeps the
handler path simple: `SESSION_START` changes state, while the scenario layer
decides when to enqueue Cherry-aligned range-data notifications through
`on_session_started` and `on_session_stopped` hooks, while the engine clock
decides when scheduled events are ready to emit. That gives the simulator a
cleaner path toward more realistic asynchronous delivery without leaking
scenario behavior into handlers or transport. `SESSION_STOP` cancels any
remaining stream notifications before they reach the transport.
