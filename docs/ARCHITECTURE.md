# Architecture

## Boundaries

- `spec`: constants and protocol interpretation aligned to Qorvo SDK definitions
- `profile`: device-specific visible defaults, capability payloads, and timing behavior
- `core`: packet parsing, framing helpers, the device engine / outbound queue, and clock abstraction
- `model`: mutable simulated device state
- `handlers`: command behavior that mutates the model and emits responses/notifications
- `scenario`: notification policy and deterministic behavior variants layered on top of the model
- `transport`: raw byte I/O adapters

## V1 Transport Strategy

TCP comes first because it is simple to automate, inspect, and test. The
transport now talks to a device engine, not directly to the handlers. Host
packets are submitted into the engine, the engine advances its internal clock,
and outbound packets are drained from the engine queue.
The engine clock is injectable: tests use explicit/manual time progression,
while runtime transports can attach a wall-clock source.

## Evolution Plan

Future transports should only adapt raw bytes to `uci_sim_packet_t` and the
engine API. Session/ranging scenarios should live in dedicated model or
scenario modules rather than transport or CLI code. Future runtime work should
continue to keep “what time is it?” outside the engine logic itself.

## Scenario Direction

The simulator now has an explicit scenario seam through `uci_sim_scenario.*`
and a small internal scheduled-event queue in the device core. The default
scenario preserves current immediate notification behavior, and future variants
should change notification timing or error injection by scheduling or canceling
events through scenario hooks instead of pushing test-only branches into the
transport adapter or the core device model.

The simulator now also has an explicit device-profile seam through
`uci_sim_profile.*`. The current default profile owns the versions, capability
payload, default device configs, default session data size, and ranging timing
that were previously scattered across initialization code, handlers, and the
engine. It now also owns the supported command/config/app-config matrix, which
the handlers enforce before touching model storage. Future fidelity work should
add new profiles for concrete hardware and firmware targets instead of editing
generic simulator logic.
Session transition rules now live there too, so valid start/stop states, next
states, and invalid-transition status codes are profile behavior rather than
hard-coded handler assumptions. The range-data notification shape is also
profile-owned: the model copies a Cherry-aligned payload template from the
profile and patches only the dynamic fields before enqueueing the notification.

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

Phase 1 of the Qorvo-SDK-driven fidelity work now has an explicit internal
measurement-policy seam in `uci_sim_measurement.*`. The current behavior is
intentionally unchanged: the policy layer still reproduces the same range-data
notification bytes and proximity-gating rules as before. The architectural
gain is that range notification generation is no longer a single function that
both decides policy and patches packet bytes directly. Future work for
`RESULT_REPORT_CONFIG`, `AOA_RESULT_REQ`, `RSSI_REPORTING`, and
`RANGING_INTERVAL` should extend that measurement-policy seam rather than
adding more branches to the device model or handlers.

`RESULT_REPORT_CONFIG` and `AOA_RESULT_REQ` are now the first behavioral users
of that seam. The simulator still keeps the Cherry-aligned TWR packet layout
stable, but the measurement policy now controls which result fields remain
meaningful:
- bit 0 keeps the distance/ToF-derived field meaningful
- bit 1 keeps azimuth fields meaningful
- bit 2 keeps elevation fields meaningful
- bit 3 keeps AoA FoM fields meaningful
- `AOA_RESULT_REQ` then intersects with that policy so the simulator can gate
  azimuth and elevation independently without changing packet shape

Disabled fields are serialized as zero rather than removed from the packet.
That matches the current audit guidance: change field meaning before changing
packet shape.
