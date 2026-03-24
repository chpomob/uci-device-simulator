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

`RESULT_REPORT_CONFIG`, `AOA_RESULT_REQ`, `RSSI_REPORTING`, and
`RANGING_INTERVAL` are now the first behavioral users of that seam. The simulator still keeps the
Cherry-aligned TWR packet layout stable, but the measurement policy now
controls which result fields remain meaningful:
- bit 0 keeps the distance/ToF-derived field meaningful
- bit 1 keeps azimuth fields meaningful
- bit 2 keeps elevation fields meaningful
- bit 3 keeps AoA FoM fields meaningful
- `AOA_RESULT_REQ` then intersects with that policy so the simulator can gate
  azimuth and elevation independently without changing packet shape
- `RSSI_REPORTING` independently gates whether the RSSI byte is meaningful
- `RANGING_INTERVAL` now feeds both the visible notification interval field
  and the delay used for the full scheduled ranging stream, including the
  first post-start measurement
- `MAX_NUMBER_OF_MEASUREMENTS` now uses that same scheduler-facing seam to
  enforce a deterministic measurement budget: `0` means unlimited, while a
  finite budget stops further range production, returns the session to `IDLE`,
  and emits the matching FiRa session-status reason
- `CAP_SIZE_RANGE` now uses the scheduler/contention side of the same
  validation seam as a typed min/max pair. The default profile keeps it
  validation-only: `0x0000` is the neutral baseline, while non-zero values are
  only acceptable for a contention-based session with coherent slot topology
  and are otherwise rejected before runtime
- `SESSION_TIME_BASE` now also uses the scheduler seam in a typed form
  instead of raw packet bytes: the simulator parses enable / continue /
  resync flags, reference session id, and offset microseconds into an
  internal relationship model, aligns a dependent session’s first range event
  to the reference session’s next pending range event plus offset, optionally
  resyncs active dependents when the reference session starts, and tears down
  non-continuing dependents with `ERROR_REF_UWB_SESSION_LOST` when the
  reference session stops; `SESSION_START` also re-validates two cross-session
  timing constraints on that same seam:
  - effective ranging interval must match the reference session
  - offset must fit inside one ranging-interval window

`RANGING_ROUND_USAGE` now also participates in that seam, but with an explicit
profile boundary. The default Qorvo-like profile only accepts the FiRa
round-usage values that still map cleanly onto the current TWR measurement
layout (`SS/DS deferred`, `SS/DS non-deferred`, `ESS_TWR`, `ADS_TWR`). The
DL-TDoA and OWR-AoA values are rejected in that profile until the simulator has
separate payload builders for those measurement types.

The simulator now also has a separate validation seam in `uci_sim_validation.*`.
That layer owns the first profile-driven behavioral rejection rule:
- `RANGING_INTERVAL` below the profile minimum is rejected with
  `INVALID_RANGE`
- invalid values are not stored during `SET_APP_CONFIG`
- `SESSION_START` re-validates the effective session interval before state
- `SLOT_DURATION` now uses the same profile-owned capability pattern:
  values below the default profile’s minimum supported slot duration are
  rejected with `INVALID_PARAM`, not stored, and re-validated on
  `SESSION_START`

`STS_CONFIG` now also uses that validation seam as a security-mode selector.
The current implementation intentionally keeps the runtime effect narrow and
source-backed:
- only the documented FiRa/Cherry enum values (`0x00..0x04`) are accepted
- invalid enum values are rejected with `ERROR_INVALID_STS_CONFIG`
- `SESSION_START` re-validates the dependent security material for the modes
  the local SDK proves:
  - static STS requires `STATIC_STS_IV`
  - provisioned STS requires `SESSION_KEY`
  - provisioned responder-specific sub-session mode requires both
    `SESSION_KEY` and `SUBSESSION_KEY`

This keeps STS behavior in the validation/policy layer until there is stronger
source evidence for packet-level crypto/runtime effects.
- `STS_LENGTH` now uses that same seam too:
  - only the documented FiRa/Cherry enum values (`0x00..0x02`) are accepted
  - invalid enum values are rejected with `ERROR_INVALID_STS_LENGTH`
  - `SESSION_START` re-validates the stored value before transition
- `KEY_ROTATION_RATE` now uses that same seam too:
  - only the documented Cherry range (`0..15`) is accepted
  - invalid values are rejected with `INVALID_PARAM`
  - `SESSION_START` re-validates the stored value before transition
- `DEVICE_TYPE` now uses that same seam for the classic FiRa topology model:
  only `CONTROLEE (0x00)` and `CONTROLLER (0x01)` are accepted in the default
  profile, and `SESSION_START` re-validates the classic `DEVICE_TYPE` /
  `DEVICE_ROLE` pair for `RESPONDER` / `INITIATOR` sessions before transition
- `MULTI_NODE_MODE` now uses that same seam for the FiRa multi-peer topology
  model: only `UNICAST (0x00)`, `ONE_TO_MANY (0x01)`, and
  `MANY_TO_MANY (0x02)` are accepted in the default profile
- `SESSION_START` now also re-validates the one source-backed runtime topology
  rule for that parameter: `UNICAST` sessions must still describe a single
  peer (`NUMBER_OF_CONTROLEES == 1` and a single `DST_MAC_ADDRESS`)
- `RESULT_REPORT_CONFIG` values using bits outside the documented low four
  report flags are rejected with `INVALID_PARAM`
- invalid `RESULT_REPORT_CONFIG` values are not stored and `SESSION_START`
  also re-validates the effective stored value
- `AOA_RESULT_REQ` values outside the documented `0..3` enum are rejected with
  `INVALID_PARAM`
- invalid `AOA_RESULT_REQ` values are not stored and `SESSION_START` also
  re-validates the effective stored value
- `RSSI_REPORTING` values outside the documented `0..1` on/off range are
  rejected with `INVALID_PARAM`
- invalid `RSSI_REPORTING` values are not stored and `SESSION_START` also
  re-validates the effective stored value

That separation matters for long-term maintainability: timing/report policy
stays in measurement code, while constraint enforcement moves into validation
instead of accumulating in handlers.

Disabled fields are serialized as zero rather than removed from the packet.
That matches the current audit guidance: change field meaning before changing
packet shape.
