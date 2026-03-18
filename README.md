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
the simulator’s actual handler/profile surface.

## Build

- `make`
  Builds the simulator and runs tests.
- `make uci-device-sim`
  Builds the TCP simulator binary.
- `make test`
  Runs the regression test suite.

The test suite includes TCP interoperability coverage driven by named wire-packet fixtures for the current `uci_interactive_shell` command flow. It pins `CORE_DEVICE_INFO`, `CORE_DEVICE_RESET`, `CORE_GET_CAPS_INFO`, `CORE_QUERY_UWBS_TIMESTAMP`, `CORE_SET_CONFIG`, `CORE_GET_CONFIG`, `SESSION_INIT`, `SESSION_GET_COUNT`, `SESSION_QUERY_DATA_SIZE_IN_RANGING`, `SESSION_SET_APP_CONFIG`, `SESSION_GET_APP_CONFIG`, `SESSION_START`, `SESSION_GET_STATE`, `SESSION_STOP`, and `SESSION_GET_RANGING_COUNT` request/response/notification bytes exactly on the TCP transport, and now also checks the `delayed_notifications` scenario as a deterministic black-box variant. Those fixtures now align the simulator with the shell and Cherry-style semantics for `CORE_DEVICE_INFO` payload length, reset-response/status-notification behavior, timestamp-response shape, and session-state values, while widening core config coverage to `device_state`, `low_power_mode`, and `device_pan_id`.
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
one-shot synthetic event.

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
