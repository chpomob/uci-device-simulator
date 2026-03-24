# Qorvo SDK Implementation Plan

This document turns the behavior audit into an implementation plan for the UCI
device simulator. It is intentionally staged. The goal is to improve behavioral
fidelity without collapsing the architecture back into handler-local special
cases.

Use this document together with:

- [QORVO_SDK_BEHAVIOR_AUDIT.md](QORVO_SDK_BEHAVIOR_AUDIT.md)
- [UCI_COMMAND_MATRIX.md](UCI_COMMAND_MATRIX.md)

## Scope

This plan covers the next architecture phase for simulator fidelity:

1. measurement-policy architecture
2. validation / rejection architecture
3. scheduler integration for behavioral app-configs

It does not try to solve:

- full vendor-family behavior
- full advanced TDoA modeling
- full cryptographic behavior
- all real-device timing nuances in one pass

Those stay as later phases.

## Design Constraints

The implementation must preserve these rules.

1. Transport remains thin.
   - TCP or future serial must not own protocol behavior.

2. Handlers remain declarative.
   - handlers parse requests, call model/policy code, and build responses
   - they must not become a second engine

3. Stored config is not automatically behavioral.
   - each parameter must be classified before it affects runtime

4. Behavioral logic must be testable without transport.
   - policy and engine behavior need unit-level tests, not only TCP tests

5. New behavior must remain profile-aware.
   - future device profiles must be able to vary policy, defaults, and support
     without rewriting the engine

## Parameter Classification Model

Every app-config or related setting should be placed in exactly one or more of
these categories.

### `storage_only`

The simulator stores and returns the value faithfully.

Use for parameters where local evidence is still too weak to justify runtime
impact.

### `validation_only`

The simulator validates the parameter and may reject or raise a session reason,
but the value does not directly change runtime payload generation yet.

Examples:

- channel number
- STS config before crypto/runtime modeling

### `metadata_affects_payload`

The value changes visible notification or response content.

Examples:

- result-report flags
- AoA request behavior
- RSSI reporting
- address mode

### `scheduler_affects_timing`

The value changes event timing or time-grid behavior.

Examples:

- ranging interval
- session time base
- future slot/time-grid parameters

### `state_machine_affects_behavior`

The value changes legal transitions, emitted notifications, or session runtime
state.

Examples:

- later security-mode enforcement
- later topology/session coupling

## Target Architecture

The next implementation phase should add three explicit subsystems.

### 1. Measurement Policy Layer

Purpose:

- derive meaningful measurement output from session config + profile + runtime
  state

Responsibilities:

- decide whether a measurement should produce a notification
- decide which measurement fields are meaningful
- patch or suppress measurement-domain fields before serialization
- evaluate proximity state
- later evaluate AoA state

Recommended internal model:

- measurement sample
  - session handle
  - sequence number
  - measurement type
  - short / extended peer identity
  - distance in explicit internal units
  - local AoA azimuth / elevation + FoM
  - remote AoA azimuth / elevation + FoM
  - RSSI
  - NLOS
  - slot index
  - validity bits / report bits

- measurement policy result
  - emit notification or suppress
  - emitted field mask
  - transition flags
  - measurement-type selection from session mode / ranging-round usage

This layer should sit between:

- scenario or engine event generation
- packet serialization

It should not live in handlers.

Current progress on this layer:

- report controls (`RESULT_REPORT_CONFIG`, `AOA_RESULT_REQ`, `RSSI_REPORTING`)
  are already behavioral
- `RANGING_INTERVAL` already drives emitted interval fields and engine timing
- `SLOTS_PER_RR` now also crosses the seam indirectly by constraining valid
  slot topology and by sourcing the emitted slot index from session state
- `SCHEDULED_MODE` now uses the validation layer with a profile-owned support
  mask; the default profile accepts only `TIME_SCHEDULED` until contention and
  hybrid scheduling are implemented as real engine behavior

### 2. Validation Layer

Purpose:

- centralize parameter-level and combination-level validity rules

Responsibilities:

- request validation during `SET_APP_CONFIG`
- cross-parameter validation on session start
- future mapping to session reason codes
- clean separation between:
  - direct command rejection
  - accepted config that later causes idle/error

Recommended output model:

- validation result
  - `ok`
  - `command_reject(status)`
  - `session_reason(reason_code)`
  - optional offending parameter id

This layer should not directly emit packets. It should return structured
results to handlers and engine/state-machine code.

### 3. Scheduler Policy Layer

Purpose:

- let timing-related app-configs affect engine behavior without pushing timing
  math into handlers or scenarios

Responsibilities:

- compute next ranging event time
- expose current visible ranging interval to notification serializer
- future session synchronization through `SESSION_TIME_BASE`
- future slot/time-grid behavior

This layer should compose with the engine clock abstraction that already
exists.

## Implementation Order

The order below is deliberate. Do not skip ahead.

### Phase 1: Introduce Measurement Policy Scaffolding

No large behavioral change yet.

Tasks:

1. Add an internal measurement structure.
2. Add a policy entry point that receives:
   - session
   - profile
   - raw generated measurement
3. Move current range-data template patching behind that entry point.
4. Preserve current behavior exactly.

Acceptance criteria:

- no wire behavior change
- existing tests still pass unchanged
- measurement generation no longer writes directly from model code to packet
  bytes

### Phase 2: Make Report Controls Behavioral

Parameters:

1. `RESULT_REPORT_CONFIG`
2. `AOA_RESULT_REQ`
3. `RSSI_REPORTING`

Tasks:

1. Define emitted-field policy from `RESULT_REPORT_CONFIG`.
2. Define AoA field validity policy from `AOA_RESULT_REQ`.
3. Define RSSI field policy from `RSSI_REPORTING`.
4. Decide current suppression model per field:
   - zero value
   - neutral value
   - field remains structurally present but marked meaningless by content

Current best rule from the audit:

- keep packet layout stable for now
- make fields meaningful or suppressed according to proven Qorvo/Cherry rules
- avoid inventing alternate packet layouts until stronger evidence exists

Acceptance criteria:

- unit tests for policy decisions without transport
- TCP black-box tests for changed notification content
- shell integration proves decoded output follows the configured reporting
  behavior

### Phase 3: Make `RANGING_INTERVAL` Behavioral

Tasks:

1. Read session-config `RANGING_INTERVAL` from scheduler policy, not only
   profile defaults.
2. Use it for next-event scheduling.
3. Serialize the same effective interval into emitted range-data header fields.
4. Decide override priority:
   - session config over profile default

Acceptance criteria:

- unit tests proving event timing changes with app-config
- notification header reflects the configured interval
- stop/start and scenario progression still behave deterministically in tests

### Phase 4: Introduce Validation Scaffolding

No broad rejection behavior yet. First build the seam.

Tasks:

1. Add a validation result type.
2. Add central validators for:
   - single-parameter constraints
   - simple cross-parameter constraints
3. Call validators from:
   - `SET_APP_CONFIG`
   - `SESSION_START`

Initial parameter set to validate because the audit supports it:

- `RANGING_INTERVAL`
- `RESULT_REPORT_CONFIG`
- `STS_CONFIG`
- `SESSION_KEY`
- `SUBSESSION_KEY`
- `NUMBER_OF_CONTROLEES`
- `DST_MAC_ADDRESS`

`RANGING_INTERVAL` specific guidance:

- treat interval validation as device/profile behavior, not CLI behavior
- use at least these future inputs:
  - `MIN_RANGING_INTERVAL_MS` capability
  - slot / tx-delay feasibility
  - min-frames-per-ranging-round feasibility
- do not assume the surfacing path yet:
  - local evidence proves the session reason `ERROR_INVALID_RANGING_INTERVAL`
  - local evidence does not yet prove whether Qorvo firmware rejects the
    command immediately, reports the failure later through session status, or
    does both depending on profile/flow

Acceptance criteria:

- handlers stop carrying ad hoc validation for these domains
- unit tests prove structured validation outcomes
- no transport coupling

Current status:

- validation seam implemented in `uci_sim_validation.*`
- `RANGING_INTERVAL` is now the first profile-driven validated parameter
- `RESULT_REPORT_CONFIG` is now the second validated parameter
- `AOA_RESULT_REQ` is now the third validated parameter
- `RSSI_REPORTING` is now the fourth validated parameter
- `STS_CONFIG` is now the fifth validated parameter
- `DEVICE_TYPE` is now the sixth validated parameter
- `MULTI_NODE_MODE` is now the seventh validated parameter
- default profile behavior:
  - minimum supported interval: `50 ms`
  - status on invalid value: `INVALID_RANGE`
  - command-path storage rejection on invalid `SET_APP_CONFIG`
  - `SESSION_START` re-validates the effective interval before transition
  - `RESULT_REPORT_CONFIG` accepts only the documented low four report bits
  - unsupported `RESULT_REPORT_CONFIG` bits are rejected with `INVALID_PARAM`
  - `AOA_RESULT_REQ` accepts only the documented `0..3` enum values
  - unsupported `AOA_RESULT_REQ` values are rejected with `INVALID_PARAM`
  - `RSSI_REPORTING` accepts only the documented `0..1` on/off values
  - unsupported `RSSI_REPORTING` values are rejected with `INVALID_PARAM`
  - `STS_CONFIG` accepts only the documented `0x00..0x04` enum values
  - `SESSION_START` re-validates the STS-dependent security material that the
    local Cherry helpers prove for static and provisioned modes
  - `DEVICE_TYPE` accepts only the documented FiRa `CONTROLEE (0x00)` and
    `CONTROLLER (0x01)` values in the default profile
  - `SESSION_START` re-validates the classic `DEVICE_TYPE` / `DEVICE_ROLE`
    pairing for `RESPONDER` / `INITIATOR` sessions
  - `MULTI_NODE_MODE` accepts only the documented FiRa `UNICAST (0x00)`,
    `ONE_TO_MANY (0x01)`, and `MANY_TO_MANY (0x02)` values in the default
    profile
  - `SESSION_START` re-validates the one source-backed runtime topology rule
    for that parameter: `UNICAST` must still describe a single peer
- delayed/session-status surfacing remains intentionally configurable work for
  later profiles once stronger firmware evidence exists

### Phase 5: Introduce Session Reason Mapping

Tasks:

1. Map supported validation failures to FiRa session reason codes where the
   audit has strong evidence.
2. Distinguish two paths:
   - command rejected immediately
   - session later transitions to idle/error with a reason
3. Keep unsupported mappings documented rather than guessed.

Acceptance criteria:

- reason codes used only where justified by audit evidence
- tests explicitly pin both command response and later notification behavior

### Phase 6: `SESSION_TIME_BASE` Architecture

Tasks:

1. Add internal representation for time-base relationship:
   - enabled
   - continue behavior
   - resync behavior
   - reference session
   - offset
2. Connect this to scheduler policy, not packet builders.
3. Initially support only safe deterministic semantics:
   - reference session existence validation
   - stored dependency
   - scheduler-visible offset model

Do not start with full multi-session realism.

Acceptance criteria:

- reference-session validation exists
- engine timing can observe the configured relationship
- tests are deterministic under manual clock control

## Deferred Phases

These are important, but not part of the immediate next implementation wave.

### AoA-Gated Notification Modes

Do not implement until the real AoA bound parameter surface exists.

Needed first:

- lower azimuth bound
- upper azimuth bound
- lower elevation bound
- upper elevation bound

Then implement:

- `SESSION_INFO_NTF_CONFIG` `0x03`
- `0x04`
- `0x06`
- `0x07`

### Security Runtime Semantics

Future phase:

- `STS_CONFIG`
- `STATIC_STS_IV`
- `SESSION_KEY`
- `SUBSESSION_KEY`
- `KEY_ROTATION`
- `KEY_ROTATION_RATE`

Target:

- validation and session-mode behavior, not fake cryptography

### Topology / Addressing Semantics

Future phase:

- `DEVICE_ROLE`
- `MULTI_NODE_MODE`
- `NUMBER_OF_CONTROLEES`
- `DEVICE_MAC_ADDRESS`
- `DST_MAC_ADDRESS`
- `MAC_ADDRESS_MODE`
- `SUB_SESSION_ID`

Target:

- affect measurement count, peer identity, and session shape

### Vendor / Diagnostics / Radar

Future subsystem:

- `QORVO_EXT1`
- `QORVO_EXT2`
- diagnostics reports
- radar parameters
- calibration / antenna-flex coupling

These should get their own audit-driven plan later.

## Testing Strategy

Every phase should add tests at three levels.

### Unit

Pin pure policy behavior:

- measurement field policy
- validation results
- scheduler timing decisions

### Engine / Model

Pin behavior without transport:

- queued event timing
- stop/reset interactions
- session policy transitions

### TCP Black-Box

Pin wire-visible behavior:

- changed notifications
- segmented responses where relevant
- error ordering where justified

### Shell Integration

Use the shell repo as a consumer check, not as the source of truth.

Pin:

- decoded output changes
- end-to-end interop

## Documentation Rules

For every behavioral change:

1. update the implementation plan only if the staged plan changes
2. update the audit only if new source-derived knowledge was learned
3. update the command matrix if protocol surface changed
4. update README when user-visible simulator behavior changed

Do not use the implementation plan to store discoveries that belong in the
audit.

## Immediate Next Work Item

The first concrete implementation item after this document should be:

1. Phase 1 measurement-policy scaffolding

Reason:

- it is the dependency for the next proven behavioral parameters
- it lets later work stay architectural instead of patch-by-patch
- it is low-risk if done with no wire behavior change first
