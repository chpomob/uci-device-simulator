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

## UCI Core Inner Workings

The local Cherry UCI core provides several important constraints for a correct
simulator architecture.

### Core Responsibilities

From `uci.h`, the UCI core is responsible for:

- packet parsing
- packet generation
- error processing
- segmentation
- reassembly
- routing

Conclusion:

- the simulator should keep transport framing separate from protocol/message
  semantics
- the simulator engine should reason in terms of complete messages, not only
  transport packets

Confidence: `proven`

### Dynamic, Chained Message Buffers

The UCI core and message builder use chained blocks (`uci_blk`) and reserve
header space while building messages.

Implication:

- a simulator that wants long-term fidelity should keep a clean separation
  between:
  - logical message construction
  - wire packet segmentation
- this validates the current simulator direction of not baking transport logic
  into protocol handlers

Confidence: `proven`

### Header / Payload Semantics

The core/message layer confirms little-endian scalar encoding and the message
builder helpers (`put_8bit`, `put_16bit`, `put_32bit`, `put_64bit`, etc.) make
that explicit.

Implication:

- simulator behavior should be built from typed field logic first, then
  serialized, rather than hand-assembling packet bytes in handlers

Confidence: `proven`

### Segmentation Model

The core/message layer and earlier transport audit show:

- control payload limit is 255 bytes
- data payload limit is treated separately
- message builders expect the core to handle segmentation / reassembly

Implication:

- large control responses such as `GET_APP_CONFIG(all)` should be treated as
  ordinary message generation followed by packet segmentation
- this validates the simulator’s recent move to support segmented non-DATA
  outbound responses

Confidence: `proven`

## Cherry Ranging Result Model

The Cherry client code gives a strong picture of what the simulator must cause
to happen for a client to experience a realistic UCI device.

### Range Data Notification Header Contract

`cherry_session_client.c` defines:

- `INFO_NTF_HEADER_SIZE = 25`
- header fields consumed from `SESSION_INFO_NTF` / `RANGE_DATA_NTF`:
  - sequence number
  - session handle
  - RFU byte
  - ranging interval in ms
  - measurement type
  - MAC addressing mode indicator
  - primary session handle
  - RFU
  - measurement count

Conclusion:

- these fields are not optional decoration
- they are the stable contract for downstream result parsing

Confidence: `proven`

### Measurement Count Means Real Parsing Work

Cherry does not treat `measurement_count` as a label only. It uses it to drive
measurement parsing loops and result object population.

Implication:

- simulator behavior work should not stop at whether a notification exists
- it must also eventually ensure the count and payload layout match the
  configured/result-producing session model

Confidence: `proven`

### TWR Measurement Semantics

`cherry_fira_client.c` parses TWR measurements into a structured result model:

- `short_addr`
- `status`
- `nlos`
- `distance_mm`
- local AoA azimuth + FoM
- local AoA elevation + FoM
- remote AoA azimuth + FoM
- remote AoA elevation + FoM
- `slot_index`
- `rssi`

Important detail:

- Cherry multiplies the UCI distance field by `10`, i.e. the notification field
  is interpreted as centimeters and the result object stores millimeters

Conclusion:

- the current simulator’s use of a centimeter-scale distance field is aligned
  with the Cherry consumer path
- future measurement-policy logic should treat distance internally with an
  explicit unit, not as an untyped integer

Confidence: `proven`

### AoA Representation

Cherry utilities and client code treat AoA values as fixed-point encoded
angles, then convert them to floating-point degrees for presentation.

Implication:

- AoA fields in simulator payloads should be thought of as measurement-domain
  data, not arbitrary bytes
- if simulator behavior is later gated by AoA bounds, that gating should
  operate on an internal AoA measurement model that serializes into these fixed
  point fields

Confidence: `proven`

### RSSI Representation

Cherry utilities convert RSSI values before presentation rather than treating
  the byte as a plain displayed integer.

Implication:

- `RSSI_REPORTING` should eventually control whether RSSI is present/meaningful
- simulator code should avoid baking display-form assumptions directly into the
  emitted payload model

Confidence: `proven`

### Utilities Show What Users Actually Care About

`util_dump.c` strongly suggests the client-visible measurement contract that a
realistic simulator should satisfy:

- distance
- local AoA
- remote AoA
- RSSI
- NLOS
- timing fields in advanced flows
- location/cfo/tof in advanced TDoA cases

Conclusion:

- the best simulator is not one that only replies to commands
- it is one that produces internally coherent measurement reports consumable by
  a real Cherry-style client stack

Confidence: `strong_inference`

## Error / Validation Knowledge Extracted From The SDK

The FiRa spec headers expose specific session reason codes such as:

- invalid ranging interval
- invalid result report config

Implication:

- these parameters are not passive storage in the real protocol
- invalid combinations should eventually surface as session-state or command
  rejection behavior, not just be accepted silently

Confidence: `strong_inference`

Current simulator implication:

- future behavior work should include parameter validation rules, not just
  payload-shape changes

## Validation / Rejection Model

The local Qorvo/Cherry sources give useful validation knowledge, but not all
of it has the same architectural value.

### What Cherry Unit Tests Really Prove

Cherry unit tests heavily exercise:

- null context handling
- null output pointer handling
- malformed response size handling
- strict response field parsing

Those tests are valuable, but they primarily prove Cherry client API behavior,
not full device-runtime behavior.

Examples:

- many `UCI_STATUS_INVALID_PARAM` expectations in
  `cherry/utest/ucitest/test_session_client.cc`
- many `UCI_STATUS_INVALID_MESSAGE_SIZE` expectations when the mocked response
  payload length is wrong

Architectural consequence:

- do not mistake all Cherry unit-test failures for device-side UCI rejection
- use them mainly to learn:
  - request/response wire shape expectations
  - strictness of Cherry parsing
  - argument-validation behavior of helper APIs

Confidence: `proven`

### Stronger Runtime Validation Evidence

The strongest device-runtime validation evidence comes from
`uci_spec_fira.h`, which defines session-state reason codes for invalid
configuration combinations and runtime failures.

These include:

- `ERROR_INVALID_RANGING_INTERVAL`
- `ERROR_INVALID_STS_CONFIG`
- `ERROR_INVALID_RFRAME_CONFIG`
- `ERROR_SESSION_KEY_NOT_FOUND`
- `ERROR_SUB_SESSION_KEY_NOT_FOUND`
- `ERROR_INVALID_PREAMBLE_CODE_INDEX`
- `ERROR_INVALID_SFD_ID`
- `ERROR_INVALID_PSDU_DATA_RATE`
- `ERROR_INVALID_PHR_DATA_RATE`
- `ERROR_INVALID_PREAMBLE_DURATION`
- `ERROR_INVALID_STS_LENGTH`
- `ERROR_INVALID_NUM_OF_STS_SEGMENTS`
- `ERROR_INVALID_NUM_OF_CONTROLEES`
- `ERROR_INVALID_DST_ADDRESS_LIST`
- `ERROR_INVALID_OR_NOT_FOUND_SUB_SESSION_ID`
- `ERROR_INVALID_RESULT_REPORT_CONFIG`
- `ERROR_INVALID_RANGING_ROUND_CONTROL_CONFIG`
- `ERROR_INVALID_RANGING_ROUND_USAGE`
- `ERROR_INVALID_MULTI_NODE_MODE`
- `ERROR_REF_UWB_SESSION_DOES_NOT_EXIST`
- `ERROR_REF_UWB_SESSION_RANGING_DURATION_MISMATCH`
- `ERROR_REF_UWB_SESSION_INVALID_OFFSET_TIME`
- `ERROR_REF_UWB_SESSION_LOST`
- `ERROR_DT_ANCHOR_RANGING_ROUNDS_NOT_CONFIGURED`
- `ERROR_DT_TAG_RANGING_ROUNDS_NOT_CONFIGURED`

Architectural consequence:

- the simulator needs a validation layer that can eventually feed:
  - direct command rejection
  - session idle/error transitions
  - `SESSION_STATUS_NTF` reason codes
- that validation should be kept separate from:
  - parameter storage
  - measurement serialization
  - transport behavior

Confidence: `proven`

### Current Audit Limit

The local sources prove that these reason codes exist, but they do not yet
fully prove:

- exactly when a real Qorvo firmware rejects during `SET_APP_CONFIG`
- exactly when it accepts then later goes idle with a reason code
- exact notification ordering for every invalid combination

So the current safe rule is:

- reason-code-aware validation architecture is required
- exact rejection timing still needs cautious implementation and later
  refinement

Confidence: `strong_inference`

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
| `0x04` | `CHANNEL_NUMBER` | PHY channel. Local FiRa/Cherry session surface uses channels `5` and `9`; broader channel support belongs to profile capabilities, not the default simulator behavior. | `proven` | Validation now; richer PHY/measurement impact later. |
| `0x0A` | `STS_INDEX` | STS index. | `strong_inference` | Security/sequence input later. |
| `0x0B` | `MAC_FCS_TYPE` | FCS mode. | `strong_inference` | Mostly packet-format/PHY metadata. |
| `0x0C` | `RANGING_ROUND_CONTROL` | Round control flags. | `weak_inference` | Likely session/control input later. |
| `0x12` | `RFRAME_CONFIG` | RFrame configuration. | `strong_inference` | Payload/measurement interpretation input. |
| `0x14` | `PREAMBLE_CODE_INDEX` | PHY preamble code. | `proven` | Cherry documents `9-12` for BPRF and `25-32` for HPRF; simulator should validate it against `PRF_MODE` and keep deeper PHY effects deferred. |
| `0x15` | `SFD_ID` | Start-of-frame delimiter selection. | `proven` | Cherry documents `0 or 2` in BPRF and `1-4` in HPRF; simulator should validate it against `PRF_MODE` and defer deeper PHY effects. |
| `0x16` | `PSDU_DATA_RATE` | PSDU rate. | `strong_inference` | PHY metadata. |
| `0x17` | `PREAMBLE_DURATION` | Preamble duration. | `strong_inference` | PHY metadata. |
| `0x18` | `LINK_LAYER_MODE` | Standard vs extended link-layer mode. | `strong_inference` | Likely packet/feature-shape input. |
| `0x19` | `DATA_REPETITION_COUNT` | Data repetition count. | `strong_inference` | Measurement/data behavior input. |
| `0x1C` | `TX_ADAPTIVE_PAYLOAD_POWER` | Adaptive TX power policy. | `weak_inference` | Lower priority; likely metadata. |
| `0x1F` | `PRF_MODE` | Cherry documents valid values `0x00..0x02` for BPRF/HPRF modes. | `proven` | Validation now; deeper PHY/measurement coupling later. |
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

## Cross-Source Confirmations And Limits

### Python Helper Layer

The local Python Qorvo helper layer is useful as a secondary source for sizes
and payload conventions.

Useful confirmations:

- `RangeDataNtfProximityNear`: 2 bytes
- `RangeDataNtfProximityFar`: 2 bytes
- `SessionInfoNtfBoundAoa`: 8 bytes
- `SessionKey`: 16 or 32 bytes
- `SessionTimeBase`: 9 bytes

Useful behavioral note from
`scripts/fira/run_fira_test_rx/README.md`:

- AoA azimuth field is zero if `AOA_RESULT_REQ = 0`
- AoA elevation field is zero if `AOA_RESULT_REQ = 0`

Architectural consequence:

- this strengthens the conclusion that `AOA_RESULT_REQ` affects measurement
  output, not only storage

Confidence: `proven`

### Python Helper Limits

The Python layer also shows where not to over-trust secondary helpers.

Examples:

- `0x1D` is named `SessionInfoNtfBoundAoa`, but the same file comments it as
  effectively RFU/reserved in that local mapping
- some lengths are explicitly ambiguous:
  - `DlTdoaAnchorLocation`: "could also be 11 or 13"
  - `SubSessionKey`: "could be 32 (!)"

Architectural consequence:

- use Python helpers as a size and tooling cross-check
- do not let them override stronger Cherry/spec evidence

Confidence: `proven`

## Vendor / Extension Knowledge Extracted So Far

The local Qorvo-specific sources provide real structural knowledge, but
significantly less runtime meaning than the standard FiRa surface.

### Qorvo Extension GIDs

From `uci_spec_qorvo.h` and `uci_spec_mcps.h`, the local SDK clearly exposes
several non-standard families:

- `QORVO_EXT1`
  - secure-element and secure-channel commands
- `QORVO_EXT2`
  - diagnostics
  - session listing
  - antenna flexibility commands
  - device statistics
  - device boot notification
  - GPIO timestamp helpers
- `QORVO_CALIB`
  - calibration reset
- `QORVO_MAC` / MCPS surface
  - start/stop/tx/rx/scan
  - scheduler configuration
  - calibration operations
  - test mode

Conclusion:

- the simulator should treat vendor/extension behavior as a distinct phase
  after the main FiRa simulator core is correct
- these families should not be mixed casually into the standard session engine

Confidence: `proven`

### Extractable Runtime Meaning

Some vendor pieces do carry enough meaning to record now.

#### Device Boot Notification

`uci_spec_qorvo.h` defines `QORVO_CORE_DEVICE_BOOT_NTF` and the boot-reason
enum includes at least:

- unknown
- fatal error reset

Conclusion:

- a high-fidelity simulator can later model boot notifications separately from
  standard `CORE_DEVICE_STATUS_NTF`
- Cherry boot handling also shows a conservative parsing rule:
  - if the payload is missing, Cherry treats the reason as fatal-error reset

Confidence: `proven`

#### FiRa Range Diagnostics

`cherry_fira_client.c` registers a handler for
`QORVO_FIRA_RANGE_DIAGNOSTICS` notifications, and Cherry allocates/free
diagnostic report structures around that path.

Conclusion:

- diagnostics are a real notification stream in the Qorvo stack
- they belong to a future observability/diagnostics layer, not to the minimal
  standard simulator core
- diagnostics are structurally richer than normal FiRa range notifications:
  - session handle
  - sequence number
  - report count
  - variable nested report data including segment metrics, AoA data, and CIR
    taps

Architectural consequence:

- diagnostics should later be implemented as a separate report family with its
  own internal model, not as a small extension of `RANGE_DATA_NTF`

Confidence: `proven`

#### Calibration / Antenna Coupling

`cherry_calib_client.c` sends `QORVO_MAC_SET_CALIBRATIONS`, and
`cherry_session_client.h` documents `ANTENNA_SET_ID` as the antenna set used by
that calibration command.

Conclusion:

- calibration and session/radar antenna selection are coupled concepts in the
  Qorvo stack
- future simulator architecture should keep calibration/antenna state in a
  separate subsystem rather than scattering it across session handlers

Confidence: `strong_inference`

#### Radar / Extended Session Parameters

The local Cherry session client header documents additional non-FiRa parameters
with meaningful comments, for example:

- radar timing parameters:
  - burst period in ms
  - sweep period in RSTU
  - sweeps per burst
- radar samples per sweep
- radar antenna set id
- radar max burst, which explicitly stops the session and moves it to idle once
  the configured number of bursts is reached
- radar sweep offset
- radar TX profile index
- selected UWB config id
- selected pulse shape combo
- sync code index
- `STS_INDEX0`
- `MAC_MODE`

Conclusion:

- the local SDK still contains significant non-FiRa session knowledge
- but it belongs to a future radar/extended-session audit phase, not to the
  immediate FiRa ranging simulator plan

Confidence: `proven`

### Vendor Audit Limit

At this point, the local vendor sources mostly provide:

- opcode inventory
- notification names
- parameter names
- some payload-size or comment-level meaning

They do not yet provide enough concrete runtime traces to justify a deep
behavioral implementation of:

- secure-element flows
- MCPS scheduler behavior
- calibration behavior
- antenna-flex runtime impact

Practical rule:

- vendor families should currently be modeled as a later subsystem with its own
  audit and plan
- they should not delay the main FiRa simulator architecture decisions now

Confidence: `strong_inference`

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

## Detailed Parameter Notes

This section goes deeper on the next highest-value parameters, because they are
the best candidates for future behavior work.

### `RANGING_INTERVAL` (`0x09`)

Sources:

- `uci_spec_fira.h`: identified as `RANGING_INTERVAL (aka RANGING_DURATION)`
- `cherry_session_client.h`: setter comment says "Interval between ranging, in
  milliseconds"
- `cherry_session_client.h`: builder path uses generic `put_int32()` and does
  not add interval-specific host validation
- `cherry_fira_client.h`: measurement/result structures expose
  `ranging_interval_ms`
- `cherry_fira_client.c`: result population copies `data->ranging_interval_ms`
- `cherry_fira.h`: TWR, TWR controlee, DL-TDoA anchor, and DL-TDoA tag session
  creation APIs all take `interval_ms` as an explicit runtime input
- `cherry_fira.c` / `cherry_ccc.c`: those higher-level flows forward the
  interval directly into `SESSION_SET_APP_CONFIG`
- local shell capability inventory exposes `SUPPORTED_MIN_RANGING_INTERVAL_MS`
  (`0xE4`), with the current shell-side default response advertising `50 ms`
- `uci_spec_fira.h`: explicit session reason
  `ERROR_INVALID_RANGING_INTERVAL (0x23)`
- `uci_spec_fira.h`: neighboring session reasons
  `ERROR_MIN_RFRAMES_PER_RR_NOT_SUPPORTED` and `ERROR_TX_DELAY_NOT_SUPPORTED`
  show that interval validity is coupled to other timing parameters, not only
  to a scalar minimum
- Cherry example apps parse `interval_ms` as operator input and reject only
  malformed numbers at CLI parsing time

Conclusion:

- `RANGING_INTERVAL` is a scheduler-facing session parameter, not just a value
  to echo back.
- It also appears in emitted result data as part of the visible measurement
  information.
- Host-side Cherry layers do not appear to enforce semantic interval bounds.
  They serialize and forward the value to the device.
- The local Qorvo/Cherry ecosystem clearly expects device-side validation to
  exist, because:
  - a minimum-supported capability is exposed
  - a specific invalid-interval session reason exists
  - related timing/session reasons show cross-parameter feasibility checks

Confidence: `proven`

Simulator implication:

- the engine should use the session-stored `RANGING_INTERVAL`, not only the
  profile default, when scheduling future ranging events
- the emitted `RANGE_DATA_NTF` should serialize the same interval value into
  the current ranging interval field
- future validation should live in the simulator/device validation layer, not
  in CLI parsing or packet builders
- interval validation will eventually need to compose with at least:
  - `MIN_RANGING_INTERVAL_MS` capability
  - slot / tx-delay feasibility
  - min-frames-per-ranging-round feasibility

Architecture implication:

- interval should move into the future measurement-policy / scheduler seam
- this is a clean case where one app-config affects both:
  - scheduler timing
  - visible payload content
- validation for this parameter should be profile/device driven and should not
  be embedded in shell-side metadata ranges
- the exact surfacing path remains unresolved:
  - immediate `SET_APP_CONFIG` rejection is plausible
  - later `SESSION_STATUS_NTF` with reason `0x23` is also plausible
  - local SDK evidence is not strong enough yet to choose one path as
    authoritative

Current gap:

- the simulator currently writes the profile interval into the payload and uses
  profile-driven timing; it does not yet let session app-config override that

### `RESULT_REPORT_CONFIG` (`0x2E`)

Sources:

- `uci_spec_fira.h`: identified as `RESULT_REPORT_CONFIG`
- `cherry_session_client.h`: explicit setter comment:
  - `b0 = TOF report`
  - `b1 = AOA Azimuth report`
  - `b2 = AOA elevation report`
  - `b3 = AOA FOM report`
  - applicable when Controlee transmits RRRM or MRM Type 3
  - default `0x01`
- Cherry getter side exposes:
  - `result_report_phase`
  - `report_tof`
  - `report_aoa_azimuth`
  - `report_aoa_elevation`
  - `report_aoa_fom`

Conclusion:

- `RESULT_REPORT_CONFIG` is a payload-shape policy control.
- It is not merely a capabilities or metadata field.
- Cherry treats it as decomposable reporting flags.

Confidence: `proven`

Simulator implication:

- this parameter should control which measurement components are meaningful or
  serialized in range-result payloads
- it belongs in a measurement-policy layer, not in handler-local code

Architecture implication:

- this is the best anchor parameter for introducing an internal measurement
  representation separate from raw packet bytes
- future fields like RSSI and AoA request should compose with it

Current gap:

- the simulator currently stores the value but does not let it change emitted
  measurement content

### `AOA_RESULT_REQ` (`0x0D`)

Sources:

- `uci_spec_fira.h`: identified as `AOA result requirement`
- local shell exposes it as a `0..3` single-byte parameter
- Cherry getter side exposes separate report semantics for:
  - AoA azimuth
  - AoA elevation
  - AoA FOM

Conclusion:

- `AOA_RESULT_REQ` is not sufficient on its own to define complete output
  payload shape, but it clearly belongs to the same reporting domain as
  `RESULT_REPORT_CONFIG`
- It should influence whether AoA results are generated or considered valid for
  a session

Confidence: `strong_inference`

Simulator implication:

- AoA-related measurement fields should not remain fixed template bytes
- the simulator should eventually derive AoA field presence/meaning from the
  combination of:
  - `AOA_RESULT_REQ`
  - `RESULT_REPORT_CONFIG`
  - later AoA-bound gating

Architecture implication:

- another reason to introduce a measurement-policy seam before more behavior
  changes

Current gap:

- the simulator stores this parameter but does not change AoA field behavior or
  report policy from it

### `RSSI_REPORTING` (`0x13`)

Sources:

- `uci_spec_fira.h`: identified as `RSSI report`
- `cherry_session_client.h`: explicit setter/getter comments say:
  - false = no report
  - true = report
- local shell decoders already know how to display RSSI when it exists in
  supported measurement layouts

Conclusion:

- `RSSI_REPORTING` is a direct reporting on/off control.
- This is a much stronger basis than many other still-stored-only parameters.

Confidence: `proven`

Simulator implication:

- if the chosen measurement/result shape includes RSSI, this parameter should
  decide whether RSSI is meaningful/present
- if the payload layout is fixed for now, the simulator should still at least
  control whether the RSSI field is populated with meaningful data or a neutral
  suppressed value

Architecture implication:

- `RSSI_REPORTING` should be implemented through the same measurement-policy
  layer as `RESULT_REPORT_CONFIG`, not as a special case in the emitter

Current gap:

- the simulator stores this parameter but does not affect measurement output

### `SESSION_TIME_BASE` (`0x48`)

Sources:

- `cherry_session_client.h`: explicit setter comment says "Sync sessions"
- parameters are:
  - enable
  - continue session if reference session is not active
  - resync
  - reference session handle
  - offset in microseconds
- Cherry unit test `set_session_session_time_baseOk` verifies a 9-byte payload
  carrying those flags plus reference session and offset
- Python helper layer also describes `SessionTimeBase` as a 9-byte structure

Conclusion:

- this is not a passive metadata parameter
- it is an explicit inter-session timing relationship
- the simulator should eventually model it in the engine/scheduler layer, not
  only store the raw bytes

Confidence: `proven` for wire meaning, `strong_inference` for runtime impact

Simulator implication:

- future simulator architecture should treat this as a dependency between two
  sessions in the engine clock domain
- this belongs to scheduler/state-machine work, not to packet-format work

Current gap:

- the simulator stores the value but does not use it to align or constrain
  session timing

### `SESSION_KEY` (`0x45`) And `SUBSESSION_KEY` (`0x46`)

Sources:

- `cherry_session_client.h` documents:
  - session key size can be 16 or 32 bytes
  - sub-session key size can be 128 or 256 bits
- Cherry unit tests verify that these parameters are serialized with their
  provided lengths in `SET_APP_CONFIG`
- `uci_spec_fira.h` exposes reason codes:
  - `ERROR_SESSION_KEY_NOT_FOUND`
  - `ERROR_SUB_SESSION_KEY_NOT_FOUND`

Conclusion:

- faithful storage is required
- these keys are not decorative blobs; they participate in real validation and
  security mode behavior
- they should eventually interact with:
  - `STS_CONFIG`
  - session start validation
  - session error reasons

Confidence: `proven` for storage/wire shape, `strong_inference` for runtime use

Simulator implication:

- keep exact length fidelity
- avoid inventing crypto behavior too early
- prepare the validation layer so future security-mode rules can require the
  right key material

Current gap:

- the simulator stores these values faithfully but does not yet let them affect
  session validity or security-related runtime behavior

## Parameter Dependency Summary

The next behavior work should follow this dependency order:

1. introduce an internal measurement-policy layer
2. move emitted measurement content decisions into that layer
3. make these parameters behavioral in this order:
   - `RESULT_REPORT_CONFIG`
   - `AOA_RESULT_REQ`
   - `RSSI_REPORTING`
   - `RANGING_INTERVAL`

Why this order:

- `RESULT_REPORT_CONFIG` defines the strongest payload-shape contract
- `AOA_RESULT_REQ` and `RSSI_REPORTING` naturally compose with it
- `RANGING_INTERVAL` then extends the same seam into scheduler behavior and the
  visible range-data header

## Audit Status

This audit is not mathematically complete, but the remaining local sources are
now yielding less new semantic information per pass.

What is now well covered:

- UCI core/message responsibilities
- Cherry range-data consumer semantics
- standard command-family semantics relevant to the simulator
- major app-config behavior domains
- validation/reason-code architecture implications
- limits of the current evidence

What still remains useful later, but is no longer blocking the next design
step:

- deeper vendor-family runtime semantics
- advanced TDoA behavior details beyond storage/shape
- exact firmware timing of invalid-combination rejection vs later idle/error

Practical conclusion:

- there is now enough audited knowledge to produce a real simulator
  implementation plan for:
  - measurement-policy architecture
  - validation architecture
  - scheduler integration for behavioral app-configs
