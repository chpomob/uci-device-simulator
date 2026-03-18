# Qorvo SDK Behavior Audit

This document records the current understanding of how a Qorvo/Cherry-aligned
UCI simulator should behave. It is an audit artifact, not an implementation
plan. The goal is to distinguish:

- behavior proven by local Qorvo/Cherry sources
- behavior implied by Cherry client code
- behavior currently assumed by this codebase
- gaps where implementation would currently be speculative

It should be updated before major simulator behavior changes.

## Source Hierarchy

Use these sources in this order.

1. Cherry / Qorvo protocol headers
   - `uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_spec_fira.h`
   - `uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_spec_qorvo.h`
   - `uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_spec_mcps.h`
   - `uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci.h`
   - `uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_message.h`
2. Cherry client behavior
   - `uci_analysis/uwb/Samples/Cherry/cherry/src/uci_client/cherry_core_client.c`
   - `uci_analysis/uwb/Samples/Cherry/cherry/src/uci_client/cherry_session_client.c`
   - `uci_analysis/uwb/Samples/Cherry/cherry/src/uci_client/include/cherry_core_client.h`
   - `uci_analysis/uwb/Samples/Cherry/cherry/src/uci_client/include/cherry_session_client.h`
   - `uci_analysis/uwb/Samples/Cherry/cherry/src/uci_client/cherry_fira_client.c`
   - `uci_analysis/uwb/Samples/Cherry/cherry/src/uci_client/include/cherry_fira_client.h`
3. Python Qorvo helpers
   - `uci_analysis/uwb/Samples/Python/UWB-Qorvo-Tools/lib/uwb-uci/uci/fira_app.py`
   - `uci_analysis/uwb/Samples/Python/UWB-Qorvo-Tools/lib/uwb-uci/uci/fira_enums.py`
4. Local shell / simulator behavior
   - current code in `uci_interactive_shell`
   - current code in `uci_device_simulator`

### Confidence Labels

- `proven`
  Behavior is directly supported by local protocol/spec or concrete Cherry
  client handling.
- `strong_inference`
  Not stated as plainly, but the local sources make the expected behavior hard
  to dispute.
- `weak_inference`
  A reasonable interpretation, but still not a strong enough basis for a
  device-faithful simulator.
- `unknown`
  Current implementation would be speculative.

## Core Architectural Conclusions

These conclusions are already strong enough to shape the simulator.

1. The simulator should be driven by a device engine, not by request/response
   convenience logic.
   - already implemented
   - reinforced by the way Cherry separates transport, message building, and
     client behavior

2. Session behavior should be derived from session state plus stored app-config,
   not from hard-coded scenario shortcuts.
   - already partly implemented
   - still incomplete for many app-config parameters

3. Range notification behavior needs an explicit measurement-policy layer.
   - `SESSION_INFO_NTF_CONFIG`
   - proximity thresholds
   - future AoA bounds
   - future report/result flags
   should all act on measurement state, not on raw payload bytes alone

4. Not every stored parameter should immediately change runtime behavior.
   The correct rule is:
   - implement runtime impact only when local Qorvo/Cherry evidence is strong
   - otherwise store/retrieve faithfully and document the gap

5. The simulator should prefer profile-driven behavior over handler-local
   special cases.
   - command support matrix
   - transition rules
   - capability surface
   - measurement template defaults
   - timing defaults

## Command Audit

This section focuses on standard command semantics that matter most for a
correct simulator runtime.

### CORE

| Command | Understanding | Confidence | Notes |
| --- | --- | --- | --- |
| `CORE_DEVICE_INFO` | Return device, MAC, PHY, and test versions. | `proven` | Cherry and local headers align. |
| `CORE_GET_CAPS_INFO` | Return capability TLVs. | `proven` | Payload content is device/profile specific. |
| `CORE_SET_CONFIG` | Mutates device-level config. | `strong_inference` | Cherry parsing is strict; avoid shell-only richer semantics when unsupported by SDK evidence. |
| `CORE_GET_CONFIG` | Returns requested device config TLVs. | `proven` | Single and multi-TLV response handling are both structurally supported. |
| `CORE_QUERY_UWBS_TIMESTAMP` | Returns status + 64-bit timestamp. | `proven` | Simulator should use device/profile-backed monotonic time. |
| `CORE_DEVICE_RESET` | Resets device state and emits device-ready status notification. | `strong_inference` | Current simulator behavior is aligned enough for v1. |
| `CORE_GENERIC_ERROR_NTF` | Error notification tied to protocol-level failures. | `strong_inference` | Good simulator behavior, but ordering fidelity may still vary by real device. |

### SESSION_CONFIG

| Command | Understanding | Confidence | Notes |
| --- | --- | --- | --- |
| `SESSION_INIT` | Allocates a session, sets type, returns handle, emits `SESSION_STATUS_NTF(INIT)`. | `proven` | Cherry/session handling aligns. |
| `SESSION_DEINIT` | Removes session runtime state. | `strong_inference` | Must clear scheduled events, notifications, and session policy state. |
| `SESSION_SET_APP_CONFIG` | Stores requested app-config TLVs; success response should follow Cherry semantics. | `proven` | Simulator already aligned to Cherry-style success response. |
| `SESSION_GET_APP_CONFIG` | Supports single, multi, and zero-count/all retrieval in this codebase. | `strong_inference` | Broader than Cherry helper path; document as intentional simulator contract. |
| `SESSION_GET_COUNT` | Returns current allocated session count. | `proven` | Pure model behavior. |
| `SESSION_GET_STATE` | Returns current session state. | `proven` | Also useful as a passive trigger point for queued background notifications in the engine. |
| `SESSION_UPDATE_CONTROLLER_MULTICAST_LIST` | Updates per-session multicast entries. | `strong_inference` | Structure is clear; real device-side security semantics may be richer later. |
| `SESSION_UPDATE_DT_ANCHOR_RANGING_ROUNDS` | Stores anchor round list. | `strong_inference` | Runtime impact still minimal. |
| `SESSION_UPDATE_DT_TAG_RANGING_ROUNDS` | Stores tag round list. | `strong_inference` | Runtime impact still minimal. |
| `SESSION_DATA_TRANSFER_PHASE_CONFIG` | Stores DTP config for session data transfer behavior. | `weak_inference` | State gating is not yet proven from local sources. |
| `SESSION_QUERY_DATA_SIZE_IN_RANGING` | Returns per-session max data size. | `strong_inference` | Device/profile-backed. |
| `SESSION_SET_HUS_CONTROLLER_CONFIG` | Stores HUS controller config blob. | `weak_inference` | Wire shape is implemented, but runtime semantics are not yet well understood. |
| `SESSION_SET_HUS_CONTROLEE_CONFIG` | Stores HUS controlee config blob. | `weak_inference` | Same gap as above. |

### SESSION_CONTROL

| Command | Understanding | Confidence | Notes |
| --- | --- | --- | --- |
| `SESSION_START` | Applies state transition, emits `SESSION_STATUS_NTF(ACTIVE)`, begins ranging event flow if scenario supports it. | `proven` | Current engine model is a good basis. |
| `SESSION_STOP` | Applies state transition, emits `SESSION_STATUS_NTF(IDLE)`, cancels remaining session events. | `proven` | Stop-aware suppression is an important correct behavior. |
| `SESSION_GET_RANGING_COUNT` | Returns number of emitted/simulated ranging events. | `strong_inference` | Must be defined carefully: count of internal ranging progress, not only visible notifications. |
| `SESSION_LOGICAL_LINK_CREATE` | Allocates a logical link and emits UWBS create notification. | `weak_inference` | Wire handling is clear; deeper device fidelity still incomplete. |
| `SESSION_LOGICAL_LINK_CLOSE` | Removes logical link and emits UWBS close notification. | `weak_inference` | Same limitation. |
| `SESSION_LOGICAL_LINK_GET_PARAM` | Returns logical link state. | `weak_inference` | Good enough structurally, not yet proven behaviorally. |

### DATA

| Command | Understanding | Confidence | Notes |
| --- | --- | --- | --- |
| `DATA_MESSAGE_SND` | DATA-plane ingress, not a control-plane command. | `proven` | Must obey Cherry `DATA` header semantics and fragmentation model. |
| `SESSION_DATA_CREDIT_NTF` | Credit feedback for DATA transfer flow. | `strong_inference` | Current simulator behavior is plausible and useful. |
| `SESSION_DATA_TRANSFER_STATUS_NTF` | DATA transfer result per sequence. | `strong_inference` | Important for client interoperability. |

### Notifications

| Notification | Understanding | Confidence | Notes |
| --- | --- | --- | --- |
| `SESSION_STATUS_NTF` | Reflects state transitions and management-driven reasons. | `proven` | Important simulator baseline. |
| `RANGE_DATA_NTF (SESSION_INFO_NTF)` | Primary ranging data notification stream. | `proven` | Core of simulator usefulness. |
| `CORE_DEVICE_STATUS_NTF` | Device status change notification. | `strong_inference` | Current simulator emits it on reset; wider policy still open. |
| Logical-link UWBS notifications | Side effects of logical-link lifecycle. | `weak_inference` | Acceptable for now. |

## App-Config Audit

The list below is organized by behavior domain, because that is the correct
implementation axis for the simulator.

### 1. Notification Policy And Measurement Gating

| ID | Parameter | Understanding | Confidence | Runtime Impact |
| --- | --- | --- | --- | --- |
| `0x0D` | `AOA_RESULT_REQ` | Controls which AoA results are requested/reported. | `strong_inference` | Should influence whether AoA fields are meaningful/present. |
| `0x0E` | `SESSION_INFO_NTF_CONFIG` | Controls range-data notification policy. | `proven` | Already behavioral for `0x00`, `0x01`, `0x02`, `0x05`. |
| `0x0F` | `RNG_DATA_NTF_PROXIMITY_NEAR` | Lower proximity threshold. | `strong_inference` | Already behavioral for proximity modes. |
| `0x10` | `RNG_DATA_NTF_PROXIMITY_FAR` | Upper proximity threshold. | `strong_inference` | Already behavioral for proximity modes. |
| `0x13` | `RSSI_REPORTING` | Whether RSSI should be reported. | `strong_inference` | Should affect payload content or report policy. |
| `0x1D` | `AOA_BOUND_CONFIG` / `rng_data_ntf_aoa_bound` | Local surface exists, but Cherry local behavior is not defined through this single value alone. | `unknown` | Do not make this behavioral yet as a Cherry-aligned AoA gate. |
| `0x2E` | `RESULT_REPORT_CONFIG` | Controls which measurement results are reported. | `strong_inference` | Should be a major future measurement-policy input. |
| `0x47` | `SESSION_DATA_TRANSFER_STATUS_NTF_CONFIG` | Controls DATA transfer status notification policy. | `weak_inference` | Likely behavioral later. |
| `0x4D` | `OWR_AOA_MEASUREMENT_NTF_PERIOD` | OWR AoA notification cadence control. | `weak_inference` | Likely scheduler input later. |

#### Critical AoA Conclusion

Do not implement AoA-gated `SESSION_INFO_NTF_CONFIG` modes from `0x1D` alone.

Cherry’s local documentation for AoA-gated modes depends on four separate bound
values:

- lower AoA azimuth
- upper AoA azimuth
- lower AoA elevation
- upper AoA elevation

Those values are documented in `cherry_session_client.h`, but the local
codebase does not currently expose them as app-config parameters. That means:

- AoA gating cannot yet be implemented in a Qorvo/Cherry-faithful way
- the next correct step is to introduce those parameters first

### 2. Timing And Scheduler Behavior

| ID | Parameter | Understanding | Confidence | Runtime Impact |
| --- | --- | --- | --- | --- |
| `0x08` | `SLOT_DURATION` | Slot duration in RSTU. | `strong_inference` | Future scheduler/time-grid input. |
| `0x09` | `RANGING_INTERVAL` | Interval between rangings. | `proven` | Should drive event scheduling. |
| `0x1A` | `RANGING_TIME_STRUCT` | Time-structure choice. | `weak_inference` | Likely time-grid behavior input. |
| `0x1B` | `SLOTS_PER_RR` | Slots per ranging round. | `strong_inference` | Future measurement scheduling input. |
| `0x21` | `TX_JITTER_WINDOW_SIZE` | Jitter window size. | `weak_inference` | Future timing/randomization input. |
| `0x2B` | `UWB_INITIATION_TIME` | Initiation/start reference time. | `weak_inference` | Scheduler/profile input later. |
| `0x33` | `UL_TDOA_TX_INTERVAL` | UL-TDoA interval. | `strong_inference` | Scheduler input for UL-TDoA later. |
| `0x34` | `UL_TDOA_RANDOM_WINDOW` | UL-TDoA randomization window. | `weak_inference` | Scheduler/randomization input. |
| `0x36` | `SUSPEND_RANGING_ROUNDS` | Round suspension count/behavior. | `weak_inference` | Scheduler/state-machine input. |
| `0x48` | `SESSION_TIME_BASE` | Time-base sync with another session. | `strong_inference` | Strong architectural impact; should not remain storage-only forever. |

### 3. Session Topology And Addressing

| ID | Parameter | Understanding | Confidence | Runtime Impact |
| --- | --- | --- | --- | --- |
| `0x00` | `DEVICE_TYPE` | Initiator vs responder role in ranging behavior. | `proven` | Should affect measurement generation and allowed flows. |
| `0x03` | `MULTI_NODE_MODE` | Unicast / anycast / multicast topology. | `strong_inference` | Should affect session structure and peer model. |
| `0x05` | `NUMBER_OF_CONTROLEES` | Number of peer controlees. | `strong_inference` | Should affect session topology and result count. |
| `0x06` | `DEVICE_MAC_ADDRESS` | Local device MAC identity. | `strong_inference` | Should affect emitted measurement addresses. |
| `0x07` | `DST_MAC_ADDRESS` | Destination MAC identity. | `strong_inference` | Should affect peer addressing in emitted measurements. |
| `0x11` | `DEVICE_ROLE` | Controller vs controlee. | `strong_inference` | Session/control behavior input. |
| `0x26` | `MAC_ADDRESS_MODE` | Short vs extended addressing. | `proven` | Should control range-data payload layout. |
| `0x30` | `SUB_SESSION_ID` | Sub-session identifier. | `strong_inference` | Important for multicast / advanced session shapes. |
| `0x4C` | `APPLICATION_DATA_ENDPOINT` | Application data endpoint. | `weak_inference` | Data-plane semantics input later. |

### 4. PHY / Measurement Shape

| ID | Parameter | Understanding | Confidence | Runtime Impact |
| --- | --- | --- | --- | --- |
| `0x01` | `RANGING_ROUND_USAGE` | One-way / two-way / data-related ranging usage. | `proven` | Should change measurement type and payload shape. |
| `0x04` | `CHANNEL_NUMBER` | PHY channel. | `proven` | Likely profile/measurement metadata input. |
| `0x0A` | `STS_INDEX` | STS index. | `strong_inference` | Security/sequence input later. |
| `0x0B` | `MAC_FCS_TYPE` | FCS mode. | `strong_inference` | Mostly packet-format/PHY metadata. |
| `0x0C` | `RANGING_ROUND_CONTROL` | Round control flags. | `weak_inference` | Likely session/control input later. |
| `0x12` | `RFRAME_CONFIG` | RFrame configuration. | `strong_inference` | Payload/measurement interpretation input. |
| `0x14` | `PREAMBLE_CODE_INDEX` | PHY preamble code. | `strong_inference` | PHY metadata; may affect capabilities/validation. |
| `0x15` | `SFD_ID` | Start-of-frame delimiter selection. | `strong_inference` | PHY metadata. |
| `0x16` | `PSDU_DATA_RATE` | PSDU rate. | `strong_inference` | PHY metadata. |
| `0x17` | `PREAMBLE_DURATION` | Preamble duration. | `strong_inference` | PHY metadata. |
| `0x18` | `LINK_LAYER_MODE` | Standard vs extended link-layer mode. | `strong_inference` | Likely packet/feature-shape input. |
| `0x19` | `DATA_REPETITION_COUNT` | Data repetition count. | `strong_inference` | Measurement/data behavior input. |
| `0x1C` | `TX_ADAPTIVE_PAYLOAD_POWER` | Adaptive TX power policy. | `weak_inference` | Lower priority; likely metadata. |
| `0x1F` | `PRF_MODE` | PRF mode selection. | `strong_inference` | PHY metadata; may affect valid combinations. |
| `0x20` | `CAP_SIZE_RANGE` | CAP size bounds. | `weak_inference` | Future scheduler/contention input. |
| `0x22` | `SCHEDULED_MODE` | Scheduled mode enable/choice. | `strong_inference` | Scheduler mode input later. |

### 5. Security

| ID | Parameter | Understanding | Confidence | Runtime Impact |
| --- | --- | --- | --- | --- |
| `0x02` | `STS_CONFIG` | Security timestamp mode. | `proven` | Must affect whether keys/STS fields matter. |
| `0x23` | `KEY_ROTATION` | Key rotation enable. | `strong_inference` | Security policy input. |
| `0x24` | `KEY_ROTATION_RATE` | Key rotation cadence. | `strong_inference` | Scheduler/security input. |
| `0x28` | `STATIC_STS_IV` | Static STS IV. | `proven` | Security material; should not be payload decoration only. |
| `0x29` | `NUMBER_OF_STS_SEGMENTS` | Number of STS segments. | `strong_inference` | Security/session-shape input. |
| `0x35` | `STS_LENGTH` | STS length. | `strong_inference` | Security/session-shape input. |
| `0x45` | `SESSION_KEY` | Provisioned session key. | `proven` | Must remain stored faithfully; runtime use later. |
| `0x46` | `SUBSESSION_KEY` | Provisioned sub-session key. | `proven` | Same as above. |

### 6. Advanced TDoA / Specialized Features

Most TDoA-specific parameters are currently understood structurally but not yet
well enough to impose strong simulator behavior without more device-level
evidence.

Treat these as:

- store/retrieve faithfully
- validate wire shape and lengths
- do not over-interpret their runtime effect yet

This includes:

- `UL_TDOA_NTF_REPORT_CONFIG`
- `UL_TDOA_DEVICE_ID`
- `UL_TDOA_TX_TIMESTAMP`
- `DL_TDOA_RANGING_METHOD`
- `DL_TDOA_TX_TIMESTAMP_CONF`
- `DL_TDOA_HOP_COUNT`
- `DL_TDOA_ANCHOR_CFO`
- `DL_TDOA_ANCHOR_LOCATION`
- `DL_TDOA_TX_ACTIVE_RANGING_ROUNDS`
- `DL_TDOA_BLOCK_STRIDING`
- `DL_TDOA_TIME_REFERENCE_ANCHOR`
- `DL_TDOA_RESPONDER_TOF`
- `SECURE_RANGING_NEFA_LEVEL`
- `SECURE_RANGING_CSW_LENGTH`

Confidence for this whole block is mostly `weak_inference`.

## Current Simulator Gaps

These are the most important correctness gaps relative to the local Qorvo/Cherry
basis.

1. Many app-config parameters are stored but not yet behavior-driving.
2. `RESULT_REPORT_CONFIG`, `AOA_RESULT_REQ`, and `RSSI_REPORTING` do not yet
   shape measurement payload content.
3. `RANGING_INTERVAL` exists as config but scheduler behavior is still mostly
   profile-driven.
4. Session topology parameters do not yet fully affect emitted peer identities
   and measurement cardinality.
5. AoA-gated notification modes cannot be made correct until the real AoA bound
   parameters are introduced.
6. Security parameters are still mostly stored-only.
7. HUS and advanced TDoA features are structurally supported but behaviorally
   under-modeled.

## Architecture Guidance For Future Work

To keep the simulator maintainable, future behavior work should follow this
structure.

### A. Build Around A Measurement Policy

Add an internal measurement representation before packet serialization.

That object should hold at least:

- distance
- local AoA azimuth
- local AoA elevation
- remote AoA azimuth
- remote AoA elevation
- RSSI / FOM / status / NLOS
- addressing information
- measurement type

Then let policy code decide:

- whether a notification should be emitted
- which fields should be meaningful/present
- how those values serialize into the packet template

### B. Separate Storage From Behavior

For each app-config parameter, decide one of:

- `storage_only`
- `validation_only`
- `metadata_affects_payload`
- `scheduler_affects_timing`
- `state_machine_affects_behavior`

Do not mix these categories casually.

### C. Introduce Behavior In Dependency Order

Recommended order:

1. measurement/report policy
   - `RESULT_REPORT_CONFIG`
   - `AOA_RESULT_REQ`
   - `RSSI_REPORTING`
2. scheduler/timing
   - `RANGING_INTERVAL`
   - slot/time-grid parameters
3. session topology/addressing
4. security modes
5. advanced TDoA / HUS behavior

### D. Do Not Guess AoA Bound Behavior

Before implementing AoA-gated notification modes:

1. add the real AoA lower/upper bound parameter surface
2. map those values to the internal measurement model
3. only then implement `SESSION_INFO_NTF_CONFIG` modes

## Immediate Next Planning Input

The highest-value next behavior work, on the basis of this audit, is:

1. `RESULT_REPORT_CONFIG`
2. `AOA_RESULT_REQ`
3. `RSSI_REPORTING`
4. `RANGING_INTERVAL`

These parameters have strong enough local evidence and they fit the same future
measurement-policy architecture.
