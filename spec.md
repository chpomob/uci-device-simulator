---
name: "uci-alignment-blocker-core-gid"
version: "1.0"
author: "adversarial-spec"
status: "draft"
tags: [adversarial, spec, uci-alignment, conformance]
targets:
  - file: src/transport/chardev/uci_sim_chardev.c
    description: "Fix stream-accumulator copy bound so it never writes past the UCI_SIM_MAX_PACKET buffer, and stop creating the PTY slave world-readable/writable"
  - file: src/handlers/uci_sim_handlers.c
    description: "Validate CORE_DEVICE_RESET's reset-config value, correct CORE_GET_DEVICE_INFO_RSP vendor-info byte layout, report a 4-byte value for capability 0xE4 in CORE_GET_CAPS_INFO_RSP, and reject writes to the read-only DEVICE_STATE (tag 0x00) config item via CORE_SET_CONFIG"
  - file: tests/test_interop_chardev.c
    description: "Add coverage for the stream-accumulator overflow fix and PTY permission bits"
  - file: tests/test_sim_core.c
    description: "Add coverage for CORE_DEVICE_RESET validation, CORE_GET_DEVICE_INFO_RSP layout, CORE_GET_CAPS_INFO_RSP capability 0xE4 value, and DEVICE_STATE write rejection"
  - file: Makefile
    description: "Add a memory-safety-instrumented build target for the chardev interop test so AC2 can be verified"
---

# UCI alignment: chardev transport safety and Core GID conformance

## Problem

An adversarial conformance review of the UWB device simulator against the
Qorvo UCI Command Interface Specification (R12.34.0-225) and related Qorvo
references found 27 retained conformance and safety defects. This spec
covers the single BLOCKER finding plus the Core GID (uci_ch04) findings from
that review — the subset with the most severe blast radius (memory
corruption reachable by any local process, and Core GID responses/behavior
that cause conforming hosts to misdetect device capabilities or accept
configuration changes the spec forbids). Remaining findings (transport
framing/fragmentation, Session GID, parameter defaults, Qorvo extensions)
are out of scope for this spec and are addressed by companion specs.

Two defects compound the severity of the transport issue: the chardev
stream accumulator can write past the end of its heap buffer when clamping
an oversized read, and the PTY slave device node is created world-readable
and world-writable, making the overflow reachable by any local process, not
just the intended host process.

Four Core GID (uci_ch04) defects cause conforming hosts to either accept
invalid device state, misread device/capability info, or successfully
change device state that the specification requires to be read-only.

## Requirements

- R1: The chardev stream accumulator must never copy more bytes into
  `dev->stream` than the remaining capacity of the `UCI_SIM_MAX_PACKET`
  buffer, including when an oversized read has already been clamped to fit.
- R2: The PTY slave device node created by the chardev transport must not
  grant read or write access to processes other than the owner and the
  group needed for legitimate host access; it must not be created with
  world-readable/world-writable permissions.
- R3: CORE_DEVICE_RESET must reject any reset-configuration value other
  than the single value the specification defines as valid (1), returning
  the specification's status code for an invalid command parameter
  (`INVALID_PARAM`) and leaving device state unchanged, instead of
  performing a reset for an invalid value.
- R4: CORE_GET_DEVICE_INFO_RSP must lay out its vendor-specific info bytes
  in exactly this field order and size (the specification's
  `CORE_GET_DEVICE_INFO_RSP` vendor-specific-information table), with no
  extra, missing, reordered, or resized field:
  1. Internal firmware version, major (1 byte)
  2. Internal firmware version, minor (1 byte)
  3. Internal firmware version, patch (1 byte)
  4. Internal firmware version, RC (1 byte)
  5. Unique firmware build identifier (8 bytes)
  6. Product firmware version, major (1 byte)
  7. Product firmware version, minor (1 byte)
  8. Product firmware version, patch (1 byte)
  9. Unique chip identifier (32 bytes)
  10. Device identifier (4 bytes)
  11. Package identifier — 0 for SoC, 1 for SiP (1 byte)
  12. Firmware flavor as a string, space-padded (24 bytes)
  13. Product ID (4 bytes)
  14. SOI variant (4 bytes)
  15. ROM code version (2 bytes)
  so a conforming host parses vendor info correctly.
- R5: CORE_GET_CAPS_INFO_RSP must report capability 0xE4
  (`AOSP_SUPPORTED_MIN_RANGING_DURATION_MS`) with a 4-byte value, not a
  0-byte value; the value must be an unsigned integer giving the minimum
  ranging interval, in milliseconds, that the device supports, and must
  equal the minimum-ranging-interval value the device already exposes
  elsewhere for the same concept, encoded in the little-endian byte order
  used by this response's other multi-byte fields.
- R6: CORE_SET_CONFIG must reject any attempt to write the DEVICE_STATE
  configuration item (tag 0x00), since the specification defines it as
  read-only, and must report it as a failed parameter rather than applying
  the write. Other valid parameter TLVs present in the same
  CORE_SET_CONFIG request must still be evaluated and applied (or reported
  failed strictly on their own merits) independent of the DEVICE_STATE
  rejection — one TLV's rejection must not cause an otherwise-valid TLV
  elsewhere in the same request to be skipped or rolled back.

## Acceptance criteria

- AC1 (R1): Feeding the chardev transport a read that, combined with
  already-buffered stream data, exceeds `UCI_SIM_MAX_PACKET` does not write
  outside the bounds of the `dev->stream` allocation, for reads up to and
  including the maximum size the transport's read buffer can hold in a
  single `read()` call; the accumulated stream length after the operation
  never exceeds `UCI_SIM_MAX_PACKET`.
- AC2 (R1): A test that reproduces the prior overflow condition (a read
  whose size, added to existing buffered bytes, exceeds
  `UCI_SIM_MAX_PACKET`) passes when `tests/test_interop_chardev.c` is built
  and run with AddressSanitizer enabled and it reports zero
  heap-buffer-overflow diagnostics; the build must exit non-zero and print
  an AddressSanitizer heap-buffer-overflow diagnostic if the overflow is
  reintroduced.
- AC3 (R2): After chardev transport initialization, the created PTY slave
  device node's permission bits do not grant read or write access to
  "other" (world).
- AC4 (R3): Sending CORE_DEVICE_RESET with reset-configuration value 1
  succeeds and performs the reset. Sending CORE_DEVICE_RESET with any other
  reset-configuration value returns status `INVALID_PARAM` and device state
  (e.g. session/config state observable via subsequent commands) is
  unchanged from before the command.
- AC5 (R4): CORE_GET_DEVICE_INFO_RSP's vendor-specific info bytes, when
  split at the offsets implied by the 15-field layout in R4 (1, 1, 1, 1, 8,
  1, 1, 1, 32, 4, 1, 24, 4, 4, 2 bytes, in that order), decode each field to
  the value the simulator holds for that field (firmware version
  major/minor/patch/RC, firmware build identifier, product firmware version
  major/minor/patch, chip identifier, device identifier, package
  identifier, firmware flavor string, product ID, SOI variant, ROM code
  version) with no field misaligned, truncated, or carrying a value from a
  different field.
- AC6 (R5): CORE_GET_CAPS_INFO_RSP includes an entry for capability 0xE4
  whose value-length byte is 4, whose value field is 4 bytes long, and
  whose 4 bytes, decoded as a little-endian unsigned integer, equal the
  device's configured minimum supported ranging interval in milliseconds.
- AC7 (R6): Sending CORE_SET_CONFIG with a TLV for tag 0x00 (DEVICE_STATE)
  returns a response listing tag 0x00 as a failed parameter, and a
  subsequent query of device state shows it unchanged from before the
  command.
- AC8 (R6): Sending CORE_SET_CONFIG with two TLVs in one request — one
  valid, writable parameter TLV followed by (or preceded by) a tag 0x00
  (DEVICE_STATE) TLV — returns a response listing tag 0x00 as failed and
  the other tag as succeeded, and a subsequent query confirms the valid
  parameter's new value was applied while device state is unchanged.
