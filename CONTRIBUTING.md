# Contributing to UCI Device Simulator

Thank you for your interest! Please follow these guidelines.

## Quick Start

```bash
# Build + test
make

# Build just the simulator binary
make uci-device-sim
# Run:  ./build/uci-device-sim <host> <port> [default|delayed_notifications|ranging_stream]

# Get help
make help
```

## Code Style

- **Language**: C11 (`-std=c11`)
- **Format**: Google style, Allman braces, 4-space indent, 120 char line limit
  - `.clang-format` file present in repository root
  - `.editorconfig` present for editor integration (Vim, VS Code, etc.)
- **Compiler flags**: `-Wall -Wextra -g -MMD -MP`
- **Zero warnings**: Every file must compile without warnings under `-Wall -Wextra`

## Commit Conventions

- **Subject line**: ≤ 72 chars, imperative mood (`add`, `fix`, `remove`, `refactor`)
- **Scope prefix** (optional but preferred): `ci:`, `docs:`, `src/`, `include/`, `test/`,
  `build:`, `style:`
- **Body**: Explain _why_ the change is needed. Reference any `QORVO_SDK_BEHAVIOR_AUDIT.md`
  finding or `UCI_COMMAND_MATRIX.md` entry when relevant.

Example:

```
sim: add SESSION_STATUS_NTF with 3s delay for delayed_notifications profile

The default profile now returns SESSION_STATUS_NTF immediately on
SESSION_START. The new profile matches Cherry DK4 behavior where
the framework takes time to configure the controller before
sending the status notification.

refs: docs/UCI_COMMAND_MATRIX.md SESSION_STATUS_NTF
```

## Project Structure

| directory         | purpose                            |
|------              |----------                    -------|
| `src/spec/`      | UCI constants, device profiles  |
| `src/core/`       | packet parsing / building, engine |
| `src/model/`       | Device / Session state            |
| `src/handlers/`   | Protocol-family response logic    |
| `src/scenario/`   | Notification policy, event sched  |
| `src/transport/`   | Transport adapters (TCP first)  |
| `tests/`           | Protocol regression tests         |

## Design Rules

1. **Qorvo SDK is truth** for all UCI constants — see `src/spec/` for pinned values.
2. **Transport adapters are thin** — no protocol logic in `src/transport/`.
3. **Behavior changes grounded in audit** — any new simulator logic must reference
   `docs/QORVO_SDK_BEHAVIOR_AUDIT.md`.
4. **Tests pin wire bytes** — the TCP interop tests capture exact request/response
   packet layouts. Any protocol change must update the fixture files in
   `tests/fixtures/tcp/`.

## Writing Tests

Tests live in `tests/`. New protocol coverage belongs in `tests/fixtures/tcp/` as
hex-packet fixture files (.json or .txt) and corresponding assertions in
`tests/test_interop_tcp.c`.

```c
// Common patterns:
// TEST_ASSERT_BUF_EQ(expected_hex, actual_buf, len);
// TEST_ASSERT_STR_EQ(expected, actual);
// TEST_ASSERT_EQ(expected_int, actual_int, "description");
```

## Review Checklist

- [ ] `make help` still shows all targets
- [ ] `make clean && make -Werror` compiles without warnings
- [ ] `make test` passes (all suites)
- [ ] New protocol coverage added to `tests/fixtures/tcp/`
- [ ] `docs/UCI_COMMAND_MATRIX.md` updated if adding a new command handler
- [ ] Design rules above followed

## Reporting Issues

- Include: simulator profile (`default` / `delayed_notifications` / `ranging_stream`)
- Include: wire packet bytes (`./build/uci-device-sim <hex>` if available)
- Include: expected vs. actual response bytes
