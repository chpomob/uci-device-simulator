# UCI Device Simulator

Standalone UCI device simulator intended to interoperate with:
- the `uci_interactive_shell` project
- any other standard UCI client using compatible transport framing

## Design Rules

- Qorvo SDK definitions are the primary source of truth for all UCI constants.
- Transport adapters must remain thin. Core protocol behavior lives outside transport code.
- TCP is the first transport. Additional transports such as pseudo-serial/chardev will be added later without changing the simulator core.
- All externally visible packet behavior should be pinned by tests.
- Scenario behavior must stay outside transport code and out of the protocol handlers whenever possible.
- Device-visible defaults and timings should come from explicit device profiles, not ad hoc constants.
- Behavior changes should be grounded in the audit recorded in
  [docs/QORVO_SDK_BEHAVIOR_AUDIT.md](docs/QORVO_SDK_BEHAVIOR_AUDIT.md) before
  they become simulator runtime logic.
- Major fidelity work should follow the staged implementation document in
  [docs/QORVO_SDK_IMPLEMENTATION_PLAN.md](docs/QORVO_SDK_IMPLEMENTATION_PLAN.md).

## Initial Scope

Version 1 supports a focused interoperable subset:
- `CORE_DEVICE_INFO`
- `CORE_DEVICE_RESET`
- `CORE_GET_CAPS_INFO`
- `CORE_QUERY_UWBS_TIMESTAMP`
- `CORE_SET_CONFIG`
- `CORE_GET_CONFIG`
- `SESSION_INIT`
- `SESSION_DEINIT`
- `SESSION_SET_APP_CONFIG`
- `SESSION_GET_APP_CONFIG`
- `SESSION_GET_STATE`
- `SESSION_GET_COUNT`
- `SESSION_UPDATE_CONTROLLER_MULTICAST_LIST`
- `SESSION_DATA_TRANSFER_PHASE_CONFIG`
- `SESSION_QUERY_DATA_SIZE_IN_RANGING`
- `SESSION_START`
- `SESSION_STOP`
- `SESSION_STATUS_NTF`
- Scenario variants selected at process start, beginning with `default` and `delayed_notifications`
  and now including `ranging_stream`

## Repository Layout

- `include/`: public internal headers shared across modules
- `src/spec/`: authoritative UCI definitions and small lookup helpers
- `src/spec/uci_sim_profile.c`: default device profile and future device-specific profiles
- `src/core/`: packet parsing/building, device-engine loop, clock abstraction, and dispatch glue
- `src/model/`: simulator device/session state
- `src/handlers/`: protocol family behavior
- `src/scenario/` and `src/spec/uci_sim_scenario.c`: scenario selection, notification policy, and event scheduling hooks
- `src/transport/`: transport adapters
- `tests/`: protocol and behavior regression tests
- `docs/`: architecture notes

See [docs/UCI_COMMAND_MATRIX.md](docs/UCI_COMMAND_MATRIX.md) for the current
supported/missing command inventory derived from local Qorvo Cherry headers and
the simulator’s actual handler/profile surface. Any protocol-surface change
must update that matrix in the same commit as the code and tests.

## Build

- `make`
  Builds the simulator and runs tests.
- `make uci-device-sim`
  Builds the TCP simulator binary.
- `make test`
  Runs the regression test suite.

The test suite includes TCP interoperability coverage driven by named wire-packet fixtures for the current `uci_interactive_shell` command flow. It pins `CORE_DEVICE_INFO`, `CORE_DEVICE_RESET`, `CORE_GET_CAPS_INFO`, `CORE_QUERY_UWBS_TIMESTAMP`, `CORE_SET_CONFIG`, `CORE_GET_CONFIG`, `SESSION_INIT`, `SESSION_GET_COUNT`, `SESSION_QUERY_DATA_SIZE_IN_RANGING`, `SESSION_SET_APP_CONFIG`, `SESSION_GET_APP_CONFIG`, `SESSION_UPDATE_CONTROLLER_MULTICAST_LIST`, `SESSION_UPDATE_DT_ANCHOR_RANGING_ROUNDS`, `SESSION_UPDATE_DT_TAG_RANGING_ROUNDS`, `SESSION_SET_HUS_CONTROLLER_CONFIG`, `SESSION_SET_HUS_CONTROLEE_CONFIG`, `SESSION_DATA_TRANSFER_PHASE_CONFIG`, `SESSION_START`, `SESSION_GET_STATE`, `SESSION_STOP`, and `SESSION_GET_RANGING_COUNT` request/response/notification bytes exactly on the TCP transport, and now also covers `DATA_MESSAGE_SND` ingress with `SESSION_DATA_CREDIT_NTF` and `SESSION_DATA_TRANSFER_STATUS_NTF` notifications. Those TCP checks now include inactive-session rejection and repeated-sequence handling in addition to the normal successful data-transfer path, plus representative negative-path validation for multicast action rejection, missing-session data-transfer phase configuration, short logical-link create/close requests, and `CORE_GENERIC_ERROR_NTF` emission on invalid command responses. The fixtures align the simulator with the shell and Cherry-style semantics for `CORE_DEVICE_INFO` payload length, reset-response/status-notification behavior, timestamp-response shape, Cherry-compatible `SESSION_SET_APP_CONFIG` success responses, session-state values, multicast list update responses, DT anchor/tag round update responses, HUS status-only response handling, data-transfer phase config status handling, and data-transfer notification payloads, while widening the default session app-config surface through `0x4D`, including `session_key`, `subsession_key`, `session_data_transfer_status_ntf_config`, `session_time_base`, `dl_tdoa_responder_tof`, `secure_ranging_nefa_level`, `secure_ranging_csw_length`, `application_data_endpoint`, and `owr_aoa_measurement_ntf_period`. `SESSION_GET_APP_CONFIG` is now pinned in all three supported retrieval modes: one requested TLV, multiple requested TLVs, and zero requested TLVs meaning "return all stored supported app-config values". The widened zero-count/get-all response now exceeds one control packet, so the TCP transport emits it as a segmented UCI response stream and the shell-side compatibility tests pin that behavior.
The same fixture suite now also covers the standard logical-link lifecycle: `SESSION_LOGICAL_LINK_CREATE`, `SESSION_LOGICAL_LINK_GET_PARAM`, `SESSION_LOGICAL_LINK_CLOSE`, and the corresponding `SESSION_LOGICAL_LINK_UWBS_CREATE_NTF` / `SESSION_LOGICAL_LINK_UWBS_CLOSE_NTF` notifications.
The unit suite also pins the main logical-link failure modes: duplicate link-id rejection, missing-link close/get-param failures, and slot-exhaustion reporting.
The sibling `uci_interactive_shell` repo also provides an opt-in `make tcp-simulator-integration-test` target that launches this simulator binary and validates a real `mode_tcp` shell session against it.

## Run

`./build/uci-device-sim 127.0.0.1 9000 [default|delayed_notifications|ranging_stream]`

The server accepts one TCP client at a time and exchanges raw UCI packets.

Internally, TCP now talks to a device engine rather than directly to the
command handlers. Host packets are submitted into the engine, the engine
advances its own clock, and outbound packets are drained from the engine queue.
The engine clock is now explicit: tests can drive time manually, while the TCP
server uses a system-clock adapter. That keeps the transport as an adapter and
makes background notification behavior closer to a real device.

The `ranging_stream` scenario emits a standard `SESSION_STATUS_NTF` on session
start and then advances a short deterministic series of Cherry-aligned
`RANGE_DATA_NTF (SESSION_INFO_NTF)` notifications through the internal scenario
event queue. `SESSION_STOP` suppresses any remaining range-data notifications,
so UCI clients can exercise stream progression and stop behavior instead of a
one-shot synthetic event. `SESSION_INFO_NTF_CONFIG` now has validated runtime
behavior for `0x00` (disable), `0x01` (enable), `0x02` (emit while the
simulated distance is inside the configured proximity window), and `0x05`
(emit on proximity enter/leave transitions). The AoA-dependent modes are still
stored until their bound parameters are made behavioral.
Range-data generation now passes through an explicit internal
measurement-policy layer before packet serialization. Phase 1 keeps the wire
behavior unchanged, but it gives the simulator the right seam for future
behavioral work on `RESULT_REPORT_CONFIG`, `AOA_RESULT_REQ`,
`RSSI_REPORTING`, and `RANGING_INTERVAL`.
`RESULT_REPORT_CONFIG`, `AOA_RESULT_REQ`, `RSSI_REPORTING`, and
`RANGING_INTERVAL` are now behavioral on the current TWR range-data path while
keeping the packet layout stable: disabled result components are emitted as
zeroed fields rather than removed from the notification, and the session
`RANGING_INTERVAL` now drives both the emitted current-ranging-interval field
and the full ranging-stream schedule. The `ranging_stream` scenario no longer
front-loads early notifications: the first range-data notification now waits
one configured interval after `SESSION_START`, and each later notification uses
that same interval again. The default Qorvo-like profile now also enforces a
minimum valid ranging interval of `50 ms`: lower values are rejected during
`SESSION_SET_APP_CONFIG` with `INVALID_RANGE`, are not stored, and `SESSION_START`
also re-validates the effective session interval before entering `ACTIVE`.
`RESULT_REPORT_CONFIG` remains the measurement-field policy for the current
TWR path, and the default Qorvo-like profile now also validates it: only the
documented low four report bits (`TOF`, `AoA azimuth`, `AoA elevation`,
`AoA FoM`) are accepted, while unsupported higher bits are rejected with
`INVALID_PARAM` and are not stored.
`AOA_RESULT_REQ` now uses the same validation seam: only the documented enum
values `0..3` are accepted, while unsupported higher values are rejected with
`INVALID_PARAM`, are not stored, and are also re-validated on `SESSION_START`.
`RANGING_TIME_STRUCT` now uses that same seam conservatively: the default
profile accepts only `0x00` and `0x01`, because the local Qorvo/Cherry sources
only prove block-based scheduling as the concrete non-RFU time-structure
concept. The simulator intentionally does not invent standalone scheduler
behavior from this parameter yet; that is deferred until `SLOTS_PER_RR`,
`BLOCK_STRIDE_LENGTH`, and `SCHEDULED_MODE` are modeled together.
`SCHEDULED_MODE` now uses that same seam too: the local Cherry surface exposes
the full enum (`CONTENTION_BASED`, `TIME_SCHEDULED`, `HYBRID`), but the default
simulator profile accepts only `TIME_SCHEDULED (0x01)` because that is the
only scheduler mode this profile actually models today. The simulator
intentionally does not invent contention-based or hybrid runtime behavior yet.
`RSSI_REPORTING` now uses that same profile-driven validation seam: only the
documented `0..1` on/off values are accepted, while unsupported higher values
are rejected with `INVALID_PARAM`, are not stored, and are also re-validated
on `SESSION_START`.
`STS_CONFIG` now uses that same seam as a security-mode selector: only the
documented Cherry/FIra enum values `0x00..0x04` are accepted, while unsupported
higher values are rejected with `INVALID_PARAM`, are not stored, and are also
re-validated on `SESSION_START` together with the security material those modes
depend on (`STATIC_STS_IV` for static STS and `SESSION_KEY` for provisioned
STS, plus `SUBSESSION_KEY` for provisioned responder-specific sub-session
mode).
`DEVICE_TYPE` now also uses the validation seam for the classic FiRa role
model: only `CONTROLEE (0x00)` and `CONTROLLER (0x01)` are accepted in the
default profile, and `SESSION_START` re-validates the classic pairing with
`DEVICE_ROLE` (`CONTROLEE` with `RESPONDER`, `CONTROLLER` with `INITIATOR`).
`MULTI_NODE_MODE` now uses that same seam for the FiRa topology model: only
`UNICAST (0x00)`, `ONE_TO_MANY (0x01)`, and `MANY_TO_MANY (0x02)` are accepted
in the default profile, and `SESSION_START` re-validates the one proven
runtime topology constraint from the local Cherry setup flows: `UNICAST`
sessions must still describe a single peer (`NUMBER_OF_CONTROLEES == 1` and a
single `DST_MAC_ADDRESS`). `SLOTS_PER_RR` is no longer storage-only either:
the default profile rejects `0`, `SESSION_START` rejects sessions whose
effective responder slot index would fall outside the configured slot count,
and emitted `RANGE_DATA_NTF` packets now source their slot index from the
session configuration instead of leaving it as a fixed template byte.
`RESPONDER_SLOT_INDEX` now uses that same slot-topology seam too: immediate
`SET_APP_CONFIG` rejects values that fall outside the configured
`SLOTS_PER_RR`, and `SESSION_START` re-validates the effective pair.
`BLOCK_STRIDE_LENGTH` now uses the same validation-first scheduler seam: it is
kept as a 1-byte FiRa/Qorvo parameter, defaults to `0` in the profile, and
non-zero values are only accepted when the effective session is both
block-based (`RANGING_TIME_STRUCT = 0x01`) and `TIME_SCHEDULED`.
At this stage that means:
- bit 0 controls the distance/ToF-derived field
- bit 1 controls azimuth fields
- bit 2 controls elevation fields
- `AOA_RESULT_REQ=0` suppresses all AoA fields
- `AOA_RESULT_REQ=1` keeps only elevation-related AoA fields meaningful
- `AOA_RESULT_REQ=2` keeps only azimuth-related AoA fields meaningful
- `AOA_RESULT_REQ=3` keeps both AoA axes meaningful
- `RSSI_REPORTING=0` suppresses the RSSI field
- `RSSI_REPORTING=1` keeps the RSSI field meaningful
- bit 3 controls AoA FoM fields

Current simulator behavior is owned by one explicit default device profile.
That profile now defines the visible device versions, capability payload,
default configs, session data-size default, and ranging timing. Future work to
match a specific real device should add new profiles rather than changing the
engine or handlers directly.
The profile also owns the supported feature matrix for command OIDs, supported
core config IDs, and supported session app-config IDs. Handler code now
enforces that matrix directly, so unsupported commands fail with
`UNKNOWN_OID` and unsupported profile-gated config IDs fail with
`INVALID_PARAM` instead of drifting through generic storage paths.
It now also owns the session transition rules and the Cherry-aligned range-data
template used by the `ranging_stream` scenario. That means state-change policy
and notification payload shape can vary by profile without changing the engine,
handlers, or transport adapters.
