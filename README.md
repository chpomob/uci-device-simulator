# UCI Device Simulator

Standalone UCI device simulator intended to interoperate with:
- the `uci_interactive_shell` project
- any other standard UCI client using compatible transport framing

## Design Rules

- Qorvo SDK definitions are the primary source of truth for all UCI constants.
- Transport adapters must remain thin. Core protocol behavior lives outside transport code.
- TCP is the first transport. Additional transports such as pseudo-serial/chardev will be added later without changing the simulator core.
- All externally visible packet behavior should be pinned by tests.

## Initial Scope

Version 1 supports a focused interoperable subset:
- `CORE_DEVICE_INFO`
- `CORE_GET_CAPS_INFO`
- `SESSION_INIT`
- `SESSION_DEINIT`
- `SESSION_GET_STATE`
- `SESSION_START`
- `SESSION_STOP`
- `SESSION_STATUS_NTF`

## Repository Layout

- `include/`: public internal headers shared across modules
- `src/spec/`: authoritative UCI definitions and small lookup helpers
- `src/core/`: packet parsing/building and dispatch glue
- `src/model/`: simulator device/session state
- `src/handlers/`: protocol family behavior
- `src/transport/`: transport adapters
- `tests/`: protocol and behavior regression tests
- `docs/`: architecture notes

## Build

- `make`
  Builds the simulator and runs tests.
- `make uci-device-sim`
  Builds the TCP simulator binary.
- `make test`
  Runs the regression test suite.

The test suite includes a TCP interoperability check driven by named wire-packet fixtures for the current `uci_interactive_shell` command flow. It currently pins `CORE_DEVICE_INFO`, `CORE_GET_CAPS_INFO`, `SESSION_INIT`, `SESSION_START`, `SESSION_GET_STATE`, and `SESSION_STOP` request/response/notification bytes exactly on the TCP transport.
The sibling `uci_interactive_shell` repo also provides an opt-in `make tcp-simulator-integration-test` target that launches this simulator binary and validates a real `mode_tcp` shell session against it.

## Run

`./build/uci-device-sim 127.0.0.1 9000`

The server accepts one TCP client at a time and exchanges raw UCI packets.
