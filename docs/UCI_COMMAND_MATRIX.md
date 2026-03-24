# UCI Command Matrix

This matrix is the current command-coverage inventory for `uci_device_simulator`.

Source of truth used for the inventory:
- standard UCI groups and OIDs from Qorvo Cherry headers in
  `/media/chpo/HDD-papa/gemini_test/uci_interactive_shell/uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_spec_fira.h`
- Qorvo vendor groups from
  `/media/chpo/HDD-papa/gemini_test/uci_interactive_shell/uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_spec_qorvo.h`
- Qorvo MAC/MCPS group from
  `/media/chpo/HDD-papa/gemini_test/uci_interactive_shell/uci_analysis/uwb/Samples/Cherry/uci/uci_core/include/uci/uci_spec_mcps.h`
- actual simulator support from:
  - [include/uci_sim_spec.h](../include/uci_sim_spec.h)
  - [src/spec/uci_sim_profile.c](../src/spec/uci_sim_profile.c)
  - [src/handlers/uci_sim_handlers.c](../src/handlers/uci_sim_handlers.c)

Status meanings:
- `supported`: implemented by the simulator and enabled in the default profile
- `profile-rejected`: recognized by the simulator inventory but intentionally unsupported in the default profile
- `missing`: present in the Qorvo/Cherry definitions but not implemented in the simulator
- `not inventoried`: command family not yet modeled in the simulator spec layer

## Standard UCI Commands

| GID | OID | Command | Status | Notes |
|---|---:|---|---|---|
| `CORE` | `0x00` | `DEVICE_RESET` | `supported` | Restores profile defaults and emits `CORE_DEVICE_STATUS_NTF(READY)` |
| `CORE` | `0x01` | `DEVICE_STATUS_NTF` | `supported` | Emitted on device reset and profile-listed as a supported notification |
| `CORE` | `0x02` | `GET_DEVICE_INFO` | `supported` | Profile-backed device info response |
| `CORE` | `0x03` | `GET_CAPS_INFO` | `supported` | Profile-backed capability payload |
| `CORE` | `0x04` | `SET_CONFIG` | `supported` | Profile-gated config IDs |
| `CORE` | `0x05` | `GET_CONFIG` | `supported` | Profile-gated config IDs |
| `CORE` | `0x07` | `GENERIC_ERROR` | `supported` | Emitted as `CORE_GENERIC_ERROR_NTF` on command error responses |
| `CORE` | `0x08` | `QUERY_UWBS_TIMESTAMP` | `supported` | Profile-backed deterministic timestamp counter |

| GID | OID | Command | Status | Notes |
|---|---:|---|---|---|
| `SESSION_CONFIG` | `0x00` | `SESSION_INIT` | `supported` | Profile-backed default session type/state |
| `SESSION_CONFIG` | `0x01` | `SESSION_DEINIT` | `supported` | Model-backed deallocation |
| `SESSION_CONFIG` | `0x02` | `SESSION_STATUS_NTF` | `supported` | Profile-backed reason code |
| `SESSION_CONFIG` | `0x03` | `SET_APP_CONFIG` | `supported` | Profile-gated app config IDs |
| `SESSION_CONFIG` | `0x04` | `GET_APP_CONFIG` | `supported` | Supports single-item, multi-item, and zero-count "return all stored supported TLVs" retrieval |
| `SESSION_CONFIG` | `0x05` | `GET_COUNT` | `supported` | Model-backed |
| `SESSION_CONFIG` | `0x06` | `GET_STATE` | `supported` | Model-backed |
| `SESSION_CONFIG` | `0x07` | `UPDATE_CONTROLLER_MULTICAST_LIST` | `supported` | Model-backed multicast add/remove with per-entry status payloads |
| `SESSION_CONFIG` | `0x08` | `UPDATE_DT_ANCHOR_RANGING_ROUNDS` | `supported` | Model-backed stored round index list |
| `SESSION_CONFIG` | `0x09` | `UPDATE_DT_TAG_RANGING_ROUNDS` | `supported` | Model-backed stored round index list |
| `SESSION_CONFIG` | `0x0B` | `QUERY_DATA_SIZE_IN_RANGING` | `supported` | Model-backed |
| `SESSION_CONFIG` | `0x0C` | `SET_HUS_CONTROLLER_CONFIG` | `supported` | Stores controller-side HUS phase payload with status-only response |
| `SESSION_CONFIG` | `0x0D` | `SET_HUS_CONTROLEE_CONFIG` | `supported` | Stores controlee-side HUS phase payload with status-only response |
| `SESSION_CONFIG` | `0x0E` | `DATA_TRANSFER_PHASE_CONFIG` | `supported` | Model-backed repetition/control/payload storage |

| GID | OID | Command | Status | Notes |
|---|---:|---|---|---|
| `SESSION_CONTROL` | `0x00` | `SESSION_START` | `supported` | Profile-backed transition policy |
| `SESSION_CONTROL` | `0x00` | `SESSION_INFO` / `RANGE_DATA_NTF` | `supported` | Cherry-aligned range-data notification from profile template |
| `SESSION_CONTROL` | `0x01` | `SESSION_STOP` | `supported` | Profile-backed transition policy |
| `SESSION_CONTROL` | `0x03` | `GET_RANGING_COUNT` | `supported` | Model-backed |
| `SESSION_CONTROL` | `0x04` | `DATA_CREDIT_NTF` | `supported` | Emitted after `DATA_MESSAGE_SND` ingress |
| `SESSION_CONTROL` | `0x05` | `DATA_TRANSFER_STATUS_NTF` | `supported` | Emitted after `DATA_MESSAGE_SND` ingress |
| `SESSION_CONTROL` | `0x07` | `LOGICAL_LINK_CREATE` | `supported` | Model-backed logical-link allocation with UWBS create notification |
| `SESSION_CONTROL` | `0x08` | `LOGICAL_LINK_CLOSE` | `supported` | Model-backed logical-link close with UWBS close notification |
| `SESSION_CONTROL` | `0x09` | `LOGICAL_LINK_UWBS_CLOSE` | `supported` | Emitted on successful logical-link close |
| `SESSION_CONTROL` | `0x0A` | `LOGICAL_LINK_UWBS_CREATE` | `supported` | Emitted on successful logical-link create |
| `SESSION_CONTROL` | `0x0B` | `LOGICAL_LINK_GET_PARAM` | `supported` | Returns mode and credit for active logical links |

## Config Coverage

### Core Config IDs

Cherry/Qorvo standard device config parameters are intentionally small in the standard set.

| Config ID | Name | Status | Notes |
|---|---:|---|---|
| `0x00` | `DEVICE_STATE` | `supported` | Read/write in current simulator model |
| `0x01` | `LOW_POWER_MODE` | `supported` | Model-backed |
| `0xA2` | `DEVICE_PAN_ID` | `supported` | Qorvo/MCPS-style extension used by current interop path |
| `0xA0+` | MCPS device configs | `missing` | Present in `uci_spec_mcps.h`, not yet modeled |

### Session App Config IDs

Standard session app-config coverage is still narrow, but the default profile
now supports multi-item and zero-count `GET_APP_CONFIG` retrieval for the
stored supported TLVs below. This now includes the basic ranging/session control fields that shape PHY/session behavior in the default profile.

| Config ID | Name | Status | Notes |
|---|---:|---|---|
| `0x00` | `DEVICE_TYPE` | `supported` | Validated runtime behavior. The default profile accepts only classic FiRa `CONTROLEE (0x00)` / `CONTROLLER (0x01)` values and re-validates the classic `DEVICE_TYPE` / `DEVICE_ROLE` pairing on `SESSION_START` for `RESPONDER` / `INITIATOR` sessions. |
| `0x01` | `RANGING_ROUND_USAGE` | `supported` | Validated runtime behavior. The default profile currently accepts the TWR-family FiRa values (`0x01`, `0x02`, `0x03`, `0x04`, `0x07`, `0x08`) and rejects `OWR_DL_TDOA` / `OWR_AOA` until the simulator has matching payload models. |
| `0x02` | `STS_CONFIG` | `supported` | Validated runtime behavior. The default profile accepts the five Cherry/FiRa STS enum values (`0x00..0x04`) and re-validates required security material on `SESSION_START`: `STATIC_STS_IV` for static STS, `SESSION_KEY` for provisioned STS, and both `SESSION_KEY` plus `SUBSESSION_KEY` for provisioned responder-specific sub-session mode. |
| `0x03` | `MULTI_NODE_MODE` | `supported` | Default-profile stored/retrievable |
| `0x04` | `CHANNEL_NUMBER` | `supported` | Default-profile stored/retrievable |
| `0x05` | `NUMBER_OF_CONTROLEES` | `supported` | Default-profile stored/retrievable |
| `0x06` | `DEVICE_MAC_ADDRESS` | `supported` | Default-profile stored/retrievable |
| `0x07` | `DST_MAC_ADDRESS` | `supported` | Default-profile stored/retrievable |
| `0x08` | `SLOT_DURATION` | `supported` | Default-profile stored/retrievable and validated against the profile minimum supported slot duration capability |
| `0x09` | `RANGING_INTERVAL` | `supported` | Behavioral: stored as `RANGING_DURATION` in the current shell surface, drives emitted interval field and future ranging-stream timing |
| `0x0A` | `STS_INDEX` | `supported` | Default-profile stored/retrievable |
| `0x0B` | `MAC_FCS_TYPE` | `supported` | Default-profile stored/retrievable |
| `0x0C` | `RANGING_ROUND_CONTROL` | `supported` | Default-profile stored/retrievable |
| `0x0D` | `AOA_RESULT_REQ` | `supported` | Default-profile stored/retrievable |
| `0x0E` | `SESSION_INFO_NTF_CONFIG` | `supported` | Stored/retrievable; validated runtime behavior for `0x00` disable, `0x01` enable, `0x02` emit while inside the configured proximity window, and `0x05` emit on proximity enter/leave transitions. AoA-dependent modes remain stored until AoA gating is implemented. |
| `0x0F` | `RNG_DATA_NTF_PROXIMITY_NEAR` | `supported` | Stored/retrievable; validated runtime impact on `SESSION_INFO_NTF_CONFIG` proximity-gated modes |
| `0x10` | `RNG_DATA_NTF_PROXIMITY_FAR` | `supported` | Stored/retrievable; validated runtime impact on `SESSION_INFO_NTF_CONFIG` proximity-gated modes |
| `0x11` | `DEVICE_ROLE` | `supported` | Default-profile stored/retrievable; classic `RESPONDER` / `INITIATOR` values are now re-checked against `DEVICE_TYPE` on `SESSION_START` in the default profile. |
| `0x12` | `RFRAME_CONFIG` | `supported` | Default-profile stored/retrievable |
| `0x13` | `RSSI_REPORTING` | `supported` | Default-profile stored/retrievable |
| `0x14` | `PREAMBLE_CODE_INDEX` | `supported` | Default-profile stored/retrievable |
| `0x15` | `SFD_ID` | `supported` | Default-profile stored/retrievable |
| `0x16` | `PSDU_DATA_RATE` | `supported` | Default-profile stored/retrievable |
| `0x17` | `PREAMBLE_DURATION` | `supported` | Default-profile stored/retrievable |
| `0x18` | `LINK_LAYER_MODE` | `supported` | Default-profile stored/retrievable |
| `0x19` | `DATA_REPETITION_COUNT` | `supported` | Default-profile stored/retrievable; now also drives repeated data-transfer progression and ongoing-transfer rejection |
| `0x1A` | `RANGING_TIME_STRUCT` | `supported` | Default-profile stored/retrievable; now conservatively validated to `0..1`, with scheduler impact intentionally deferred |
| `0x1B` | `SLOTS_PER_RR` | `supported` | Default-profile stored/retrievable |
| `0x1C` | `TX_ADAPTIVE_PAYLOAD_POWER` | `supported` | Default-profile stored/retrievable |
| `0x1D` | `RNG_DATA_NTF_AOA_BOUND` | `supported` | Default-profile stored/retrievable |
| `0x1E` | `RESPONDER_SLOT_INDEX` | `supported` | Default-profile stored/retrievable |
| `0x1F` | `PRF_MODE` | `supported` | Default-profile stored/retrievable |
| `0x20` | `CAP_SIZE_RANGE` | `supported` | Default-profile stored/retrievable and validated as a typed contention min/max pair; non-zero values are rejected in the default time-scheduled profile |
| `0x21` | `TX_JITTER_WINDOW_SIZE` | `supported` | Default-profile stored/retrievable |
| `0x22` | `SCHEDULED_MODE` | `supported` | Default-profile stored/retrievable |
| `0x23` | `KEY_ROTATION` | `supported` | Default-profile stored/retrievable |
| `0x24` | `KEY_ROTATION_RATE` | `supported` | Default-profile stored/retrievable and validated against the Cherry `0..15` range |
| `0x25` | `SESSION_PRIORITY` | `supported` | Default-profile stored/retrievable |
| `0x26` | `MAC_ADDRESS_MODE` | `supported` | Default-profile stored/retrievable |
| `0x27` | `VENDOR_ID` | `supported` | Default-profile stored/retrievable |
| `0x28` | `STATIC_STS_IV` | `supported` | Default-profile stored/retrievable |
| `0x29` | `NUMBER_OF_STS_SEGMENTS` | `supported` | Default-profile stored/retrievable |
| `0x2A` | `MAX_RR_RETRY` | `supported` | Default-profile stored/retrievable |
| `0x2B` | `UWB_INITIATION_TIME` | `supported` | Default-profile stored/retrievable |
| `0x2C` | `HOPPING_MODE` | `supported` | Default-profile stored/retrievable |
| `0x2D` | `BLOCK_STRIDE_LENGTH` | `supported` | Default-profile stored/retrievable |
| `0x2E` | `RESULT_REPORT_CONFIG` | `supported` | Default-profile stored/retrievable |
| `0x2F` | `IN_BAND_TERMINATION_ATTEMPT_COUNT` | `supported` | Default-profile stored/retrievable |
| `0x30` | `SUB_SESSION_ID` | `supported` | Default-profile stored/retrievable |
| `0x31` | `BPRF_PHR_DATA_RATE` | `supported` | Default-profile stored/retrievable |
| `0x32` | `MAX_NUMBER_OF_MEASUREMENTS` | `supported` | Default-profile stored/retrievable; `0` means unlimited, finite values now stop the ranging stream with `SESSION_STATUS_NTF(MAX_NUMBER_OF_MEASUREMENTS_REACHED)` |
| `0x33` | `UL_TDOA_TX_INTERVAL` | `supported` | Default-profile stored/retrievable |
| `0x34` | `UL_TDOA_RANDOM_WINDOW` | `supported` | Default-profile stored/retrievable |
| `0x35` | `STS_LENGTH` | `supported` | Default-profile stored/retrievable and validated against the Cherry/FIra `0..2` enum |
| `0x36` | `SUSPEND_RANGING_ROUNDS` | `supported` | Default-profile stored/retrievable |
| `0x37` | `UL_TDOA_NTF_REPORT_CONFIG` | `supported` | Default-profile stored/retrievable |
| `0x38` | `UL_TDOA_DEVICE_ID` | `supported` | Default-profile stored/retrievable |
| `0x39` | `UL_TDOA_TX_TIMESTAMP` | `supported` | Default-profile stored/retrievable |
| `0x3A` | `MIN_FRAMES_PER_RR` | `supported` | Default-profile stored/retrievable |
| `0x3B` | `MTU_SIZE` | `supported` | Default-profile stored/retrievable |
| `0x3C` | `INTER_FRAME_INTERVAL` | `supported` | Default-profile stored/retrievable |
| `0x3D` | `DL_TDOA_RANGING_METHOD` | `supported` | Default-profile stored/retrievable |
| `0x3E` | `DL_TDOA_TX_TIMESTAMP_CONF` | `supported` | Default-profile stored/retrievable |
| `0x3F` | `DL_TDOA_HOP_COUNT` | `supported` | Default-profile stored/retrievable |
| `0x40` | `DL_TDOA_ANCHOR_CFO` | `supported` | Default-profile stored/retrievable |
| `0x41` | `DL_TDOA_ANCHOR_LOCATION` | `supported` | Default-profile stored/retrievable as the minimal 1-byte location-presence form |
| `0x42` | `DL_TDOA_TX_ACTIVE_RANGING_ROUNDS` | `supported` | Default-profile stored/retrievable |
| `0x43` | `DL_TDOA_BLOCK_STRIDING` | `supported` | Default-profile stored/retrievable |
| `0x44` | `DL_TDOA_TIME_REFERENCE_ANCHOR` | `supported` | Default-profile stored/retrievable |
| `0x45` | `SESSION_KEY` | `supported` | Default-profile stored/retrievable; simulator and shell accept 16-byte and 32-byte hex values |
| `0x46` | `SUBSESSION_KEY` | `supported` | Default-profile stored/retrievable; simulator and shell accept 16-byte and 32-byte hex values |
| `0x47` | `SESSION_DATA_TRANSFER_STATUS_NTF_CONFIG` | `supported` | Default-profile stored/retrievable |
| `0x48` | `SESSION_TIME_BASE` | `supported` | Default-profile uses the 9-byte local Qorvo/Cherry structure, now with deterministic scheduler behavior for reference alignment, optional resync, reference-loss stop semantics, and start-time rejection on interval mismatch / out-of-window offset |
| `0x49` | `DL_TDOA_RESPONDER_TOF` | `supported` | Default-profile stored/retrievable |
| `0x4A` | `SECURE_RANGING_NEFA_LEVEL` | `supported` | Default-profile stored/retrievable |
| `0x4B` | `SECURE_RANGING_CSW_LENGTH` | `supported` | Default-profile stored/retrievable |
| `0x4C` | `APPLICATION_DATA_ENDPOINT` | `supported` | Default-profile stored/retrievable as the inferred minimal 1-byte endpoint form |
| `0x4D` | `OWR_AOA_MEASUREMENT_NTF_PERIOD` | `supported` | Default-profile stored/retrievable |
| `0x00`..`0x4D` | Standard FiRa app-config range | `supported` | Default-profile stored/retrievable coverage now spans the full standard range; `0x41` and `0x4C` currently use the local minimal forms noted above |
| `0xA0`..`0xEC` | CCC / vendor app-config extensions | `missing` | Not yet modeled |

## Vendor and Extended Groups

These are present in the local Qorvo SDK headers but not yet modeled in the simulator spec/dispatch surface.

| GID | Family | Status | Notes |
|---|---|---|---|
| `0x09` | `QORVO_EXT1` | `not inventoried` | Secure-element / binding command set |
| `0x0B` | `QORVO_EXT2` | `not inventoried` | Diagnostics, session enumeration, antenna flexibility, stats |
| `0x0C` | `ANDROID` | `not inventoried` | Android extension family not modeled |
| `0x0D` | `TEST` | `not inventoried` | Test command family not modeled |
| `0x0E` | `QORVO_MAC` | `not inventoried` | MCPS/MAC command family from `uci_spec_mcps.h` |
| `0x0F` | `QORVO_CALIB` | `not inventoried` | Calibration reset family |

## Data and SE-Testing Message Types

| Message Type | Status | Notes |
|---|---|---|
| `DATA` | `supported` | `DATA_MESSAGE_SND` ingress is modeled and emits transfer-status/credit notifications |
| `SE_TESTING_COMMAND/RESPONSE` | `missing` | Not modeled in simulator spec or transport |

## Current Coverage Summary

### Supported standard commands
- `CORE`: `DEVICE_RESET`, `DEVICE_STATUS_NTF`, `GET_DEVICE_INFO`, `GET_CAPS_INFO`, `SET_CONFIG`, `GET_CONFIG`, `QUERY_UWBS_TIMESTAMP`
- `SESSION_CONFIG`: `SESSION_INIT`, `SESSION_DEINIT`, `SESSION_STATUS_NTF`, `SET_APP_CONFIG`, `GET_APP_CONFIG`, `GET_COUNT`, `GET_STATE`, `UPDATE_CONTROLLER_MULTICAST_LIST`, `SET_HUS_CONTROLLER_CONFIG`, `SET_HUS_CONTROLEE_CONFIG`, `DATA_TRANSFER_PHASE_CONFIG`, `QUERY_DATA_SIZE_IN_RANGING`
- `SESSION_CONTROL`: `SESSION_START`, `SESSION_STOP`, `GET_RANGING_COUNT`, `LOGICAL_LINK_CREATE`, `LOGICAL_LINK_CLOSE`, `LOGICAL_LINK_GET_PARAM`
- Notifications: `SESSION_STATUS_NTF`, Cherry-aligned `RANGE_DATA_NTF (SESSION_INFO_NTF)`, `DATA_CREDIT_NTF`, `DATA_TRANSFER_STATUS_NTF`, `LOGICAL_LINK_UWBS_CREATE`, `LOGICAL_LINK_UWBS_CLOSE`
- `DATA`: `DATA_MESSAGE_SND` ingress with model-backed transfer-status behavior

### Highest-priority missing standard commands
1. broader `CORE_DEVICE_STATUS_NTF` policy beyond reset-triggered readiness
2. deeper device-profile-specific error ordering/notification behavior
3. remaining standard session app-config IDs
4. more realistic async notification ordering under error conditions
5. richer profile variants beyond the current default surface

### Highest-priority missing profile/config coverage
1. `DEVICE_MAC_ADDRESS`/`DST_MAC_ADDRESS` multi-peer variants
2. profile-specific unsupported-value/error behavior per parameter
3. richer notification/reporting parameter interactions
4. addressing and scheduling profile variants beyond current defaults
5. additional standard FiRa app-config IDs beyond the current ranging-focused slice

## Notes

- The default simulator profile is intentionally stricter than the full Qorvo SDK surface.
- Some commands appear in the local simulator spec header but are still `profile-rejected` because the handler/model path does not implement them yet.
- Any protocol-surface change must update this matrix in the same commit as the code and tests.
- The next implementation work should use this matrix as the backlog, grouped by family instead of adding isolated commands ad hoc.
