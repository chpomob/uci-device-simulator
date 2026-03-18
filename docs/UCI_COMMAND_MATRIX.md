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
| `SESSION_CONFIG` | `0x0C` | `SET_HUS_CONTROLLER_CONFIG` | `missing` | Not yet modeled |
| `SESSION_CONFIG` | `0x0D` | `SET_HUS_CONTROLEE_CONFIG` | `missing` | Not yet modeled |
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
stored supported TLVs below.

| Config ID | Name | Status | Notes |
|---|---:|---|---|
| `0x00` | `DEVICE_TYPE` | `supported` | Default-profile stored/retrievable |
| `0x01` | `RANGING_ROUND_USAGE` | `missing` | In Cherry/Qorvo headers, not yet modeled |
| `0x02` | `STS_CONFIG` | `missing` | Not yet modeled |
| `0x03` | `MULTI_NODE_MODE` | `supported` | Default-profile stored/retrievable |
| `0x04` | `CHANNEL_NUMBER` | `missing` | Not yet modeled |
| `0x05` | `NUMBER_OF_CONTROLEES` | `missing` | Not yet modeled |
| `0x06` | `DEVICE_MAC_ADDRESS` | `missing` | Not yet modeled |
| `0x07` | `DST_MAC_ADDRESS` | `missing` | Not yet modeled |
| `0x08` | `SLOT_DURATION` | `missing` | Not yet modeled |
| `0x09` | `RANGING_INTERVAL` | `missing` | Simulator uses a profile timing value but not app-config command support yet |
| `0x11` | `DEVICE_ROLE` | `supported` | Default-profile stored/retrievable |
| `0x0A`..`0x4D` | Standard FiRa app-config range | `missing` | Not yet modeled |
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
- `SESSION_CONFIG`: `SESSION_INIT`, `SESSION_DEINIT`, `SESSION_STATUS_NTF`, `SET_APP_CONFIG`, `GET_APP_CONFIG`, `GET_COUNT`, `GET_STATE`, `UPDATE_CONTROLLER_MULTICAST_LIST`, `DATA_TRANSFER_PHASE_CONFIG`, `QUERY_DATA_SIZE_IN_RANGING`
- `SESSION_CONTROL`: `SESSION_START`, `SESSION_STOP`, `GET_RANGING_COUNT`, `LOGICAL_LINK_CREATE`, `LOGICAL_LINK_CLOSE`, `LOGICAL_LINK_GET_PARAM`
- Notifications: `SESSION_STATUS_NTF`, Cherry-aligned `RANGE_DATA_NTF (SESSION_INFO_NTF)`, `DATA_CREDIT_NTF`, `DATA_TRANSFER_STATUS_NTF`, `LOGICAL_LINK_UWBS_CREATE`, `LOGICAL_LINK_UWBS_CLOSE`
- `DATA`: `DATA_MESSAGE_SND` ingress with model-backed transfer-status behavior

### Highest-priority missing standard commands
1. `SET_HUS_CONTROLLER_CONFIG`
2. `SET_HUS_CONTROLEE_CONFIG`
3. broader `CORE_DEVICE_STATUS_NTF` policy beyond reset-triggered readiness
4. deeper device-profile-specific error ordering/notification behavior
5. remaining standard session app-config IDs

### Highest-priority missing profile/config coverage
1. `RANGING_ROUND_USAGE`
2. `STS_CONFIG`
3. `CHANNEL_NUMBER`
4. `NUMBER_OF_CONTROLEES`
5. `DST_MAC_ADDRESS`
6. `RANGING_INTERVAL`
7. `RESULT_REPORT_CONFIG` / notification-related app-configs

## Notes

- The default simulator profile is intentionally stricter than the full Qorvo SDK surface.
- Some commands appear in the local simulator spec header but are still `profile-rejected` because the handler/model path does not implement them yet.
- Any protocol-surface change must update this matrix in the same commit as the code and tests.
- The next implementation work should use this matrix as the backlog, grouped by family instead of adding isolated commands ad hoc.
