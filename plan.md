---
spec: "uci-alignment-blocker-core-gid"
version: "1.0"
author: "adversarial-plan"
based-on: "adversarial-spec"
findings-input: true
---

# Implementation Plan

## Steps

### P1: Chardev transport safety — stream-accumulator overflow and PTY world-permission fixes (R1, R2)
- **Files:** [src/transport/chardev/uci_sim_chardev.c]
- **Description:**
  Fixes two co-located defects in `uci_sim_chardev_process_input()` and
  `uci_sim_chardev_start()`.

  **R1 (overflow):** In `uci_sim_chardev_process_input()` (currently around
  lines 250–266), the existing clamp logic computes `need = dev->slen + n`,
  and when `need > UCI_SIM_MAX_PACKET` it clamps `need` and shifts bytes
  *within the local `buf`* via `memmove(buf, buf + n - (UCI_SIM_MAX_PACKET -
  dev->slen), UCI_SIM_MAX_PACKET - dev->slen)` — but the subsequent
  `memcpy(dev->stream + dev->slen, buf, n)` still copies the *original*,
  unclamped `n` bytes, writing past the end of the `dev->stream` allocation
  (which is sized to exactly `UCI_SIM_MAX_PACKET`, allocated at line ~260).
  Replace this with an unambiguous clamp-then-copy:
  1. Capture `old_slen = dev->slen` before mutating anything.
  2. Compute `copy_len = (size_t)n`; if `old_slen + copy_len >
     UCI_SIM_MAX_PACKET`, set `copy_len = UCI_SIM_MAX_PACKET - old_slen`.
  3. Ensure `dev->stream` is allocated (`realloc` only when `dev->stream ==
     NULL`, sized to `UCI_SIM_MAX_PACKET`).
  4. `memcpy(dev->stream + old_slen, buf, copy_len);` and set `dev->slen =
     old_slen + copy_len;` — this is always `<= UCI_SIM_MAX_PACKET` by
     construction, so no separate re-clamp of `dev->slen` is needed.
  5. Remove the now-dead in-`buf` `memmove` entirely — dropping the excess
     trailing bytes of an over-capacity read is acceptable (the data no
     longer fits a well-formed packet stream by definition) as long as nothing
     is written outside `dev->stream`'s bounds.
  Add a one-line comment on the clamp explaining *why* `copy_len` (not `n`)
  must be used for the `memcpy`, since this is exactly the invariant the
  prior bug violated silently.

  **R2 (PTY world-permission):** In `uci_sim_chardev_start()`, `chmod(sl_name,
  0666)` (currently line 183) grants world read/write on the slave PTY node.
  Change the mode to `0660` (owner + group `rw`, no `other`) so only the
  simulator's own user/group can open the slave device, satisfying R2/AC3
  while still allowing the intended host process to open it (the host process
  must run as the same user or a member of the owning group — this is already
  true for every existing test and the `main_chardev.c` entry point, which
  open the returned `pty_path` from the same process tree). Update the
  comment above the `chmod()` call to state the new intent (owner+group only,
  not world).
- **Dependencies:** []
- **Tests:** Covered by P6 (tests/test_interop_chardev.c) — this step only
  changes production code; do not hand-write ad hoc tests here since P6 adds
  the dedicated AC1/AC2/AC3 coverage against this exact fix.
- **Risks:** An off-by-one in `copy_len` calculation could still under- or
  over-copy; guard with the invariant `old_slen + copy_len ==
  min(old_slen + n, UCI_SIM_MAX_PACKET)` and verify against P6's AC1/AC2
  tests, including the AddressSanitizer target from P7, before considering
  this step done. Tightening the PTY mode to `0660` could, in principle,
  break a caller that relies on cross-group or cross-user access to the slave
  path; grep confirms the only in-repo openers (`main_chardev.c`,
  `tests/test_interop_chardev.c`) run in the same process/user as the
  simulator, so this is safe for this codebase, but note it in the PR
  description as a behavior change for any external tooling that opens the
  PTY as a different user.

### P2: CORE_DEVICE_RESET reset-configuration value validation (R3)
- **Files:** [src/handlers/uci_sim_handlers.c]
- **Description:**
  In `handle_core()`'s `UCI_CORE_DEVICE_RESET` case (currently lines
  342–350), the handler validates only `payload_len == 1` and then
  unconditionally calls `uci_sim_device_reset_runtime_state(device)` for
  *any* single-byte payload value. Add a value check: if
  `request->payload[0] != 1`, call `make_status_response(request, result,
  UCI_STATUS_INVALID_PARAM)` and `return -1` *without* calling
  `uci_sim_device_reset_runtime_state()` and without emitting the
  `emit_device_status_ntf()` notification — device state and any other
  observable state must be left exactly as it was before the command
  (R3/AC4). Only when `payload[0] == 1` does the existing reset-and-notify
  path run. Use a named constant (e.g. `#define
  UCI_CORE_DEVICE_RESET_CONFIG_VALID 1U` near the top of the file, alongside
  the existing `UCI_SIM_DEVICE_INFO_FIXED_LEN` define) instead of a bare
  literal `1`, so the valid value is self-documenting at the call site.
- **Dependencies:** []
- **Tests:** Covered by P5 (tests/test_sim_core.c) — this step changes
  production code only.
- **Risks:** `tests/test_sim_core.c`'s existing
  `test_core_device_reset_restores_profile_defaults` (around line 1710-1719)
  sends `request.payload[0] = 0x00` for `UCI_CORE_DEVICE_RESET` and asserts
  it *succeeds*. That value becomes invalid under R3/AC4. P5 must update this
  literal to `0x01` (the only valid value) or the whole test suite will fail
  to build a passing baseline after this step. Flagging here so the dev loop
  does not treat that test failure as a regression in P2.

### P3: Profile static-data corrections — DEVICE_INFO vendor layout and CAPS 0xE4 value (R4, R5)
- **Files:** [src/spec/uci_sim_profile.c, include/uci_sim_profile.h]
- **Description:**
  Both fixes are static-data corrections to `k_default_profile` in
  `src/spec/uci_sim_profile.c`; `uci_sim_handlers.c`'s
  `UCI_CORE_DEVICE_INFO` and `UCI_CORE_GET_CAPS_INFO` cases already copy
  these blobs verbatim (memcpy at handlers.c line 379, and lines 393–396
  respectively), so no handler code changes are needed for either fix —
  only the underlying profile data was wrong. (Note: the spec's frontmatter
  names `src/handlers/uci_sim_handlers.c` as the target for these two fixes;
  the actual byte layout lives in the profile data this handler blits
  unmodified, so this step corrects the data at its source instead of adding
  a redundant reshuffle in the handler.)

  **R4 (vendor-info layout):** Replace the `.vendor_specific_data` initializer
  (currently lines 33–50) and its preceding comment (lines 20–31) with the
  exact 15-field, 86-byte layout from spec R4, in order:
  1. fw major (1) 2. fw minor (1) 3. fw patch (1) 4. fw RC (1)
  5. unique firmware build identifier (8)
  6. product fw major (1) 7. product fw minor (1) 8. product fw patch (1)
  9. unique chip identifier (32)
  10. device identifier (4)
  11. package identifier — 0=SoC, 1=SiP (1)
  12. firmware flavor string, space-padded, not NUL-padded (24)
  13. product ID (4) 14. SOI variant (4) 15. ROM code version (2)
  Total = 1+1+1+1+8+1+1+1+32+4+1+24+4+4+2 = 86 bytes, matching the existing
  `.vendor_specific_length = 86` (keep that field unchanged — only the byte
  *contents/order* were wrong, not the declared length). Preserve the
  existing device identity values where the field concept carries over
  (device_id 0x8BED, product_id 0x8BED, SOI variant 2, ROM rev 1, flavor
  string derived from `"QM35825_SIP_V1.0"`), but re-home each into its
  correct offset and width per the new field list; pad the flavor string
  with ASCII spaces (`0x20`), not `0x00`, to fill all 24 bytes, per R4 item
  12 ("space-padded"). The "unique chip identifier" field is new (32 bytes,
  not present as a distinct field in the old layout's 16-byte `soc_id`) —
  populate it with the same `"QM35825"` identifier data used previously for
  `soc_id`, zero/space-padded to 32 bytes, with a comment noting it is a
  placeholder identity value consistent with the rest of the static profile.
  Update `include/uci_sim_profile.h`'s doc-comment on
  `vendor_specific_data`/`vendor_specific_length` (if one exists near line
  29-30) to reference the new 15-field layout instead of the old
  "qm-firmware format" description, so the header and the data stay in sync.

  **R5 (capability 0xE4 value):** Replace `.core_caps_payload = {
  UCI_STATUS_OK, 0x01, 0xE4, 0x00 }` / `.core_caps_payload_len = 4`
  (currently lines 199–200) with a TLV that carries a 4-byte little-endian
  value equal to `supported_min_ranging_interval_ms` (line 60, currently
  `50U`). Introduce a `#define UCI_SIM_DEFAULT_MIN_RANGING_INTERVAL_MS 50U`
  near the top of `uci_sim_profile.c` (or in `uci_sim_profile.h` if other
  files will reference it), use it for `.supported_min_ranging_interval_ms =
  UCI_SIM_DEFAULT_MIN_RANGING_INTERVAL_MS`, and derive the CAPS bytes from
  the same macro via constant-expression byte extraction so the two values
  can never drift apart:
  ```c
  .core_caps_payload = {
      UCI_STATUS_OK, 0x01,
      0xE4, 0x04,
      (uint8_t)(UCI_SIM_DEFAULT_MIN_RANGING_INTERVAL_MS & 0xFFU),
      (uint8_t)((UCI_SIM_DEFAULT_MIN_RANGING_INTERVAL_MS >> 8) & 0xFFU),
      0x00, 0x00
  },
  .core_caps_payload_len = 8,
  ```
  (`supported_min_ranging_interval_ms` is a `uint16_t`, so the top two bytes
  of the 4-byte LE value are always `0x00, 0x00` for any value that field can
  hold; the macro-derived low two bytes still keep the CAPS entry and the
  field in lockstep.)
- **Dependencies:** []
- **Tests:** Covered by P5 (tests/test_sim_core.c) — this step changes
  profile data only.
- **Risks:** `tests/test_sim_core.c`'s
  `test_core_device_info_clamps_vendor_length_to_payload_limit` (around line
  1265) and any other test asserting specific byte offsets/values within
  `CORE_GET_DEVICE_INFO_RSP`'s vendor-info region, or the exact
  `core_caps_payload_len`/byte count for `UCI_CORE_GET_CAPS_INFO`
  (`test_core_caps_match_profile`, around line 1627), will need their
  expected offsets/lengths updated in P5 to match the corrected 86-byte
  layout and the new 8-byte CAPS entry — a byte-for-byte layout change is a
  breaking change for any prior test (or real host) hard-coding the old
  offsets, which is precisely R4/R5's point (the old offsets were
  spec-nonconforming).

### P4: CORE_SET_CONFIG DEVICE_STATE read-only enforcement (R6)
- **Files:** [src/handlers/uci_sim_handlers.c, src/model/uci_sim_device.c]
- **Description:**
  `handle_core_set_get_config()`'s `is_set` branch (currently lines 448–481)
  currently `break`s out of the TLV loop entirely on the first invalid/failed
  TLV (unsupported config id, oversized value, or store failure), which
  violates R6/AC8's requirement that one TLV's rejection must not skip or
  roll back an otherwise-valid TLV elsewhere in the same request. Rework the
  loop:
  1. Add a config-id check immediately after parsing `config_id`/`value_len`:
     if `config_id == UCI_DEVICE_CONFIG_DEVICE_STATE` (tag 0x00), record this
     TLV as failed (write `result->response.payload[response_offset++] =
     config_id; result->response.payload[response_offset++] =
     UCI_STATUS_INVALID_PARAM;`), do **not** call
     `uci_sim_device_store_config()` for it, set an `int any_failed = 1;`
     local flag, `offset += value_len; processed++;` and `continue` to the
     next TLV — do not `break`.
  2. For every other existing failure branch in the loop
     (`uci_sim_profile_supports_core_config()` returning false,
     `uci_sim_device_store_config()` returning nonzero, and the
     size/bounds check), change `break` to: write the TLV's `(config_id,
     status)` pair to the response the same way, set `any_failed = 1`,
     `continue` instead of `break` — except the bounds-overflow check
     (`response_offset + 2 > UCI_SIM_MAX_PAYLOAD` / `offset + value_len >
     request->payload_len`) which is a malformed-request/buffer-safety
     condition, not a per-TLV semantic failure, and must keep its current
     `break` (there is no way to safely continue parsing a request whose
     TLV framing itself is inconsistent, or to write further response bytes
     once the response buffer is full).
  3. On success, keep the existing `(config_id, UCI_STATUS_OK)` write.
  4. After the loop, set `result->response.payload[0] = any_failed ?
     UCI_STATUS_INVALID_PARAM : UCI_STATUS_OK;` instead of the current
     `processed != count` check (which no longer applies now that the loop
     no longer stops early on semantic failures — `processed` should equal
     `count` whenever the request's TLV framing is well-formed, regardless of
     any individual TLV's accept/reject outcome).
  5. As defense-in-depth (not a substitute for the handler-level check, since
     `uci_sim_device_get_config()` must still let `DEVICE_STATE` be *read*),
     add a guard at the top of `uci_sim_device_store_config()` in
     `src/model/uci_sim_device.c` (currently line 641): if `config_id ==
     UCI_DEVICE_CONFIG_DEVICE_STATE`, `return -1;` immediately, before the
     existing `if (config_id == UCI_DEVICE_CONFIG_DEVICE_STATE) { ...
     device->device_state = value[0]; }` block — then delete that now-dead
     write block (lines ~651–656), so DEVICE_STATE can never be written
     through this function by any current or future caller, not only the one
     caller this plan is aware of (see caller table below).
- **Dependencies:** [P2] (both P2 and P4 edit `src/handlers/uci_sim_handlers.c`
  — in different functions, `handle_core()` vs `handle_core_set_get_config()`,
  so there is no functional coupling — but declaring the dependency makes the
  intended sequencing in the Ordering rationale below enforceable by the dev
  loop instead of merely aspirational, and avoids a merge conflict if both
  steps' diffs land in the same region of the file's surrounding context)
- **Tests:** Covered by P5 (tests/test_sim_core.c) — this step changes
  production code only.
- **Risks:** The response-format change (always writing a `(config_id,
  status)` pair per TLV, success or failure, instead of omitting failed ones)
  changes `result->response.payload_len` for any request that contains a
  failure — any existing test asserting an exact `payload_len` or exact
  trailing-byte content for a `CORE_SET_CONFIG` request containing an invalid
  TLV must be updated in P5. Since `uci_sim_device_store_config()` is now the
  sole place DEVICE_STATE is ever written and that write path is removed
  entirely, `uci_sim_device_reset_runtime_state()` (used by P2) must be
  double-checked to confirm it sets `device->device_state` directly (not
  through `store_config()`) so `CORE_DEVICE_RESET` continues to restore
  `UCI_DEVICE_STATE_READY` correctly — re-verify this during P4's
  implementation, not just during P2's.

**Caller table for `uci_sim_device_store_config()` (behavior change: tag
0x00 writes now always fail):**

| File | Function/Method | Migration Note |
|------|----------------|----------------|
| src/handlers/uci_sim_handlers.c:463 | `handle_core_set_get_config()` (is_set branch) | Already special-cased to reject tag 0x00 *before* calling `store_config()` in this same step (P4.1); the added `store_config()`-level guard (P4.5) is a second, independent line of defense reachable from this same call site. |
| tests/test_sim_core.c:1512-1522 (`test_core_device_config_storage`) | Test driver calling `uci_sim_device_handle_packet()` → `handle_core_set_get_config()` → `store_config()` | Currently asserts a DEVICE_STATE `SET_CONFIG` succeeds and updates `device.device_state` — must be rewritten in P5 to use a writable tag (e.g. `UCI_DEVICE_CONFIG_LOW_POWER_MODE`) for the success assertion, plus a new assertion that a DEVICE_STATE write is now rejected. |
| tests/test_sim_core.c:1686-1692 (`test_core_device_reset_restores_profile_defaults`) | Same call chain, used only to force `device_state` to a non-default value before exercising reset | Must be rewritten in P5 to set `device.device_state = UCI_DEVICE_STATE_ACTIVE;` directly on the struct (already legal — the test file already reads this field directly for assertions) instead of going through the now-rejected `SET_CONFIG` path. |

No other in-repo callers exist (confirmed by grep across `src/`, `tests/`,
`include/`); `uci_sim_device_get_config()` (the read path) is unaffected and
continues to serve DEVICE_STATE reads for `CORE_GET_CONFIG`, which AC7/AC8
depend on to observe the post-rejection unchanged value.

### P5: tests/test_sim_core.c — Core GID conformance coverage (R3, R4, R5, R6 / AC4–AC8)
- **Files:** [tests/test_sim_core.c]
- **Description:**
  1. **Fix conflicting existing tests** (must happen in the same commit as
     the new coverage, since they will fail to build a passing baseline
     otherwise — see Risks in P2, P3, P4):
     - `test_core_device_reset_restores_profile_defaults` (~line 1675): change
       `request.payload[0] = 0x00;` for `UCI_CORE_DEVICE_RESET` to `0x01`
       (the only valid reset-config value per R3); replace the preceding
       `CORE_SET_CONFIG` DEVICE_STATE block (~lines 1686–1692) with a direct
       `device.device_state = UCI_DEVICE_STATE_ACTIVE;` assignment.
     - `test_core_device_config_storage` (~line 1502): change the SET/GET
       pair currently exercising `UCI_DEVICE_CONFIG_DEVICE_STATE` to use
       `UCI_DEVICE_CONFIG_LOW_POWER_MODE` (or another already-writable tag)
       so it continues to validate normal SET_CONFIG round-tripping; the
       DEVICE_STATE-specific assertions move to the new `AC7`/`AC8` tests
       below.
     - `test_core_device_info_clamps_vendor_length_to_payload_limit` (~line
       1265) and `test_core_caps_match_profile` (~line 1627): update any
       hard-coded offsets/lengths that assumed the old (incorrect)
       `vendor_specific_data` layout or the old 4-byte `core_caps_payload` to
       match P3's corrected 86-byte layout and 8-byte CAPS entry.
  2. **R3/AC4 — `test_core_device_reset_rejects_invalid_config`:** table-drive
     (or duplicate the test body for) at least two invalid values —
     `payload[0] = 0x00` (the value the pre-fix handler used to accept; this
     is the known regression case and must be exercised directly, not only
     indirectly via the `0x00 → 0x01` literal change in the fixed
     `test_core_device_reset_restores_profile_defaults`) and `payload[0] =
     0x02` (an arbitrary other invalid value, to confirm the check is not
     merely "reject 0") — and for each, assert the response status is
     `UCI_STATUS_INVALID_PARAM`, assert `uci_sim_device_handle_packet()`
     returns nonzero (existing convention: non-OK status returns -1), and
     assert `device.device_state` (and, e.g., an existing session's state if
     one is active) is unchanged from before the command — mirror the
     setup/assertion style of `test_core_device_reset_restores_profile_defaults`.
     Also assert the positive case (`payload[0] = 0x01`) still succeeds, if
     not already covered by the fixed existing test.
  3. **R4/AC5 — `test_core_device_info_vendor_layout`:** issue
     `UCI_CORE_DEVICE_INFO`, take the response payload starting at the
     10-byte fixed header (`UCI_SIM_DEVICE_INFO_FIXED_LEN`), split it at
     offsets `1,1,1,1,8,1,1,1,32,4,1,24,4,4,2` per R4/AC5, and assert each
     field decodes to the exact value written into `k_default_profile` by
     P3 (firmware version bytes, build identifier, product firmware version,
     chip identifier, device identifier, package identifier, the
     space-padded flavor string, product ID, SOI variant, ROM code version).
  4. **R5/AC6 — `test_core_caps_e4_ranging_interval`:** issue
     `UCI_CORE_GET_CAPS_INFO`, locate the TLV for capability tag `0xE4`
     within the response, assert its length byte is `4`, and assert its
     4 bytes decoded little-endian equal
     `device.profile->supported_min_ranging_interval_ms` (or the
     `UCI_SIM_DEFAULT_MIN_RANGING_INTERVAL_MS` macro from P3, whichever the
     test has access to) — do not hard-code `50` redundantly in a way that
     would silently pass if P3's macro-derivation broke; assert equality
     against the same symbol P3 uses.
  5. **R6/AC7 — `test_core_set_config_rejects_device_state`:** send
     `CORE_SET_CONFIG` with a single TLV for tag `0x00`
     (`UCI_DEVICE_CONFIG_DEVICE_STATE`), assert the response lists tag
     `0x00` with a failed status, then send `CORE_GET_CONFIG` for tag `0x00`
     and assert the returned value equals the device's state from before the
     SET attempt (unchanged).
  6. **R6/AC8 — `test_core_set_config_device_state_does_not_block_others`:**
     send `CORE_SET_CONFIG` with two TLVs in one request — one writable tag
     (e.g. `UCI_DEVICE_CONFIG_LOW_POWER_MODE`) and one `0x00` TLV — in both
     orderings (writable-then-DEVICE_STATE, and DEVICE_STATE-then-writable,
     as two separate test cases or one parameterized/table-driven test
     matching this repo's existing table-driven test style), assert the
     response lists tag `0x00` failed and the other tag succeeded in both
     orderings, then `CORE_GET_CONFIG` both tags and assert the writable
     tag's new value was applied while DEVICE_STATE is unchanged.
- **Dependencies:** [P2, P3, P4]
- **Tests:** This step *is* the test step; run `make uci_test` (see existing
  `Makefile` target at line 94) and confirm all of `test_sim_core.c` passes,
  including every test listed above and every fixed existing test.
- **Risks:** Because P4 changes `CORE_SET_CONFIG`'s response byte layout for
  requests containing any failed TLV (every TLV now gets a
  `(config_id, status)` pair, not just successful ones), tests written before
  P4 lands (if the dev loop executes P5 out of order) will assert the wrong
  `payload_len`/offsets — enforce dependency ordering [P2, P3, P4] strictly.
  Table-driving the two AC8 orderings (rather than duplicating the test body)
  keeps this step from growing two near-duplicate test functions.

### P6: tests/test_interop_chardev.c — chardev overflow and PTY-permission coverage (R1, R2 / AC1, AC2, AC3)
- **Files:** [tests/test_interop_chardev.c]
- **Description:**
  1. **AC1/AC2 — `test_stream_overflow_clamped`:** reproduce the exact
     pre-fix overflow condition end-to-end over the real PTY (matching this
     file's existing pattern of `write()` to `c.slave_fd` +
     `uci_sim_chardev_process_input()`, not a unit-level call):
     - First, write and `process()` a packet whose header claims a payload
       length larger than what is actually sent (e.g. a `UCI_MT_COMMAND`
       header with `payload_len` byte set to `0xFF` but only a few payload
       bytes actually written), so `stream_feed_to_engine()` sees
       `avail < pkt_total`, `break`s without consuming, and leaves those
       bytes buffered in `dev->stream` (`dev->slen > 0` — not directly
       observable from the test since `uci_sim_chardev_t` is opaque, but
       inferable from `process()` returning success with no packet echoed
       back).
     - Then write a second chunk sized at the maximum a single `read()` call
       inside `uci_sim_chardev_process_input()` can return — `UCI_SIM_MAX_PACKET`
       bytes, matching the size of that function's own `uint8_t
       buf[UCI_SIM_MAX_PACKET]` (currently line 231) — so AC1's "reads up to
       and including the maximum size the transport's read buffer can hold in
       a single `read()` call" is actually exercised, not just an arbitrary
       smaller size; `already-buffered bytes + UCI_SIM_MAX_PACKET` always
       exceeds `UCI_SIM_MAX_PACKET` regardless of how many bytes were left
       over from step 1 (any nonzero leftover already tips it over, and even
       a zero leftover makes the two equal, not over — so pad the leftover
       from step 1 to be at least 1 byte, e.g. 8 bytes of partial header, to
       guarantee the combined total strictly exceeds the buffer). Call
       `process()` again and assert it does not crash and returns without an
       unrecoverable error status that would indicate memory corruption.
     - **Verify the post-operation accumulated length is bounded, not just
       "no crash":** `uci_sim_chardev_t` is opaque, so `dev->slen` cannot be
       read directly from the test; instead, immediately after the overflow
       `process()` call, write and `process()` one more small, complete,
       well-formed command packet (reuse `test_device_reset`'s
       `UCI_CORE_DEVICE_RESET` packet, `payload[0] = 0x01`) and assert the
       *exact* expected response is read back (correct MT/GID/OID, correct
       status, correct total length) byte-for-byte. This is a meaningful
       check, not a tautology: if the clamp under test had left `dev->slen`
       corrupted or unbounded (the pre-fix bug, or any regression of it), the
       stream accumulator's notion of "how many bytes are already buffered"
       would be wrong, and the probe packet's bytes would either be
       misinterpreted as a continuation of stale buffered data (producing a
       garbled or missing response) or trigger the same out-of-bounds write
       under ASan (P7) — so a byte-exact correct response to the probe packet
       is direct evidence that `dev->slen` after the overflow operation is a
       sane, in-bounds value, satisfying AC1's "accumulated stream length
       ... never exceeds `UCI_SIM_MAX_PACKET`" from outside the opaque type.
     - Assert `process()` does not hang and the chardev context can still be
       torn down cleanly via `ctx_done()` afterward (i.e., no crash, and no
       leaked/corrupted state that makes cleanup fail).
  2. **AC3 — `test_pty_slave_not_world_accessible`:** after `ctx_init()`,
     `stat()` (or `fstat()` on a freshly `open()`ed fd to `c.pty_path`) the
     slave PTY path, and assert `(st_mode & (S_IROTH | S_IWOTH)) == 0` —
     i.e. no world read or write bit set. Use `<sys/stat.h>` (already
     available via the file's existing includes if not already pulled in
     transitively; add `#include <sys/stat.h>` explicitly if needed).
- **Dependencies:** [P1]
- **Tests:** This step *is* the test step; run `make chardev_test` (existing
  target, `Makefile` line 102) and confirm both new tests pass, then (once
  P7 lands) run the new ASan target and confirm zero heap-buffer-overflow
  diagnostics.
- **Risks:** PTY read/write chunking is not guaranteed to land in exactly one
  `read()`/`write()` system call the way the test assumes; this file's
  existing tests already rely on that assumption (e.g. `test_full_flow`), so
  it is an accepted, pre-existing pattern in this test suite. Size the first
  (leftover-inducing) write conservatively small — e.g. 8 bytes of a partial
  header, well under the kernel's default PTY buffer (typically 4096 bytes)
  — so it reliably arrives as a single `read()`; the second write is
  intentionally `UCI_SIM_MAX_PACKET` bytes (`UCI_SIM_HEADER_SIZE +
  UCI_SIM_MAX_PAYLOAD` = 1028 bytes), which is both the AC1-required maximum
  single-read size and still well under the 4096-byte PTY buffer, so it too
  should reliably land as one `read()`. If flakiness is observed (e.g. the
  kernel splits the second write into multiple `read()`s inside
  `process_input()`'s single call, under-filling `buf` and not actually
  reaching the overflow condition), retry the write/process pair with a
  short poll rather than asserting on the very first attempt, and consider
  writing the second chunk in a tight loop of `write()` calls until all
  `UCI_SIM_MAX_PACKET` bytes are queued before calling `process()`, so the
  kernel-side buffering (not the test's write pattern) is what determines
  read-call granularity.

### P7: Makefile — AddressSanitizer build target for the chardev interop test (AC2)
- **Files:** [Makefile]
- **Description:**
  Add a separate, ASan-instrumented build path for `test_interop_chardev`
  so AC2 can be verified without instrumenting every other target (which
  would slow normal `make test` runs and is unnecessary — only the chardev
  transport is in scope for R1). Concretely:
  1. New variables: `ASAN_CFLAGS = -Iinclude -Wall -Wextra -std=c11 -g
     -fsanitize=address -fno-omit-frame-pointer` and `ASAN_BUILD =
     build-asan`, placed near the existing `CFLAGS`/`BUILD` definitions
     (lines 3–7).
  2. Reuse the existing `SRCS_TEST_CHARDEV` source list (lines 57–67) — do
     not duplicate it — to derive `OBJS_TEST_CHARDEV_ASAN :=
     $(SRCS_TEST_CHARDEV:%.c=$(ASAN_BUILD)/%.o)`.
  3. A new pattern rule `$(ASAN_BUILD)/%.o: %.c` (parallel to the existing
     `$(BUILD)/%.o: %.c` rule at lines 128–130) compiling with
     `$(ASAN_CFLAGS)` into the separate `$(ASAN_BUILD)` tree, so ASan and
     non-ASan object files never collide or go stale against each other.
  4. A new link rule `$(ASAN_BUILD)/test_interop_chardev:
     $(OBJS_TEST_CHARDEV_ASAN)` linking with `$(ASAN_CFLAGS)` (the
     `-fsanitize=address` flag must be present at both compile and link
     time).
  5. A new `.PHONY` target `chardev_test_asan:
     $(ASAN_BUILD)/test_interop_chardev` that runs the binary the same way
     `chardev_test` does (line 102-104), letting a nonzero exit code (which
     ASan produces on a detected heap-buffer-overflow, per AC2) propagate as
     a `make` failure.
  6. Add `chardev_test_asan` to the `.PHONY` line (line 79) and update
     `clean` (line 134-135) to also `rm -rf $(ASAN_BUILD)`.
  7. Update `help` (lines 83-88) to mention the new target.
- **Dependencies:** [P6]
- **Tests:** Run `make chardev_test_asan`: it must build and exit 0 with the
  R1 fix from P1 and the AC2 repro test from P6 in place; as a manual sanity
  check during this step's implementation (not a permanent test artifact),
  temporarily reintroduce the original `memcpy(dev->stream + dev->slen, buf,
  n)` bug from P1 and confirm `make chardev_test_asan` now exits nonzero and
  prints an AddressSanitizer `heap-buffer-overflow` diagnostic — then revert
  that temporary change before finishing this step.
- **Risks:** `-fsanitize=address` requires a compiler/runtime that supports
  it (gcc/clang on Linux both do; this project's `Makefile` already assumes
  gcc via `CC = gcc` at line 3); if the CI/build environment lacks ASan
  support, `chardev_test_asan` would fail to build for reasons unrelated to
  R1 — this is a pre-existing environmental assumption for any ASan-gated
  target and is accepted per AC2's explicit requirement to build "with
  AddressSanitizer enabled."

### P8: Full-branch review gate
- **Files:** [src/transport/chardev/uci_sim_chardev.c, src/handlers/uci_sim_handlers.c, src/spec/uci_sim_profile.c, include/uci_sim_profile.h, src/model/uci_sim_device.c, tests/test_sim_core.c, tests/test_interop_chardev.c, Makefile]
- **Description:**
  Before this branch is proposed for merge, run one review pass over the
  **full diff of all files touched by P1–P7 together**, not the individual
  per-step commits. Per-commit review is insufficient here by construction:
  each step above was designed to compile and pass in isolation, but the
  requirements cross file boundaries in ways a single commit's diff cannot
  reveal — e.g. P4's change to `uci_sim_device_store_config()`'s DEVICE_STATE
  guard only matters correctly if P2's `CORE_DEVICE_RESET` path (which needs
  to keep setting `device->device_state` directly, not through
  `store_config()`) is inspected *alongside* it; P3's corrected
  `vendor_specific_length`/byte layout only matters correctly if
  `UCI_SIM_DEVICE_INFO_FIXED_LEN` and the `UCI_SIM_CONTROL_PAYLOAD_LIMIT`
  clamp in `uci_sim_handlers.c` (P3 does not touch this clamp, but the gate
  must confirm the 86-byte vendor blob still fits within the existing clamp
  arithmetic) are read together; and P5/P6's test fixes only prove anything
  if read against the exact final state of P1–P4's production code, not
  against an intermediate state. The gate must specifically re-check:
  1. Every AC1–AC8 acceptance criterion against the final diff, not just
     against each step's own Tests field.
  2. That no step silently reintroduced a `break`-on-first-failure pattern
     in `handle_core_set_get_config()` that P4 was meant to remove.
  3. That the caller table in P4 is still accurate against the final diff
     (no new caller of `uci_sim_device_store_config()` was added by a later
     step without being accounted for).
  4. That `make test` (all three suites: `uci_test`, `tcp_test`,
     `chardev_test`) and `make chardev_test_asan` all pass cleanly from a
     clean `make clean` build.
- **Dependencies:** [P1, P2, P3, P4, P5, P6, P7]
- **Tests:** `make clean && make test && make chardev_test_asan` must all
  succeed with zero failures and zero ASan diagnostics.
- **Risks:** This gate is the last chance to catch a fix in one file that
  silently assumed a stale version of another file's behavior (e.g. a test
  written against P3's data before P4's response-format change landed); if
  it is skipped or shortened to a per-commit review, exactly the class of
  cross-file bug this plan is most exposed to (handler ↔ profile-data ↔
  device-model coupling around `device_state` and the config TLV response
  format) is the class most likely to slip through.

## Ordering rationale

P1 (chardev transport) and P2/P3/P4 (Core GID handlers/profile data) touch
disjoint files and have no dependency on each other, but are ordered P1
before P2–P4 to match the spec's own severity ordering (the BLOCKER-severity
memory-safety issue first, then the Core GID conformance issues) and because
P1's fix is the smallest, most self-contained change, letting the dev loop
validate the harness (build, ASan target) early. P3 groups R4 and R5 together
since both are pure static-data corrections to the same struct literal in the
same file, and splitting them would create two near-conflicting edits to
adjacent lines for no isolation benefit. P4 is ordered after P2 (both edit
`uci_sim_handlers.c`, in different functions, but sequencing avoids any
chance of a merge conflict in a real dev loop) and is written with explicit
awareness of P2's reset path so the two `device_state`-touching fixes don't
silently diverge on how `device_state` may legitimately change.

P5 (Core GID tests) depends on P2, P3, and P4 because it is the acceptance
test for all three requirements' production-code changes, and — critically —
it must also *fix* three existing tests that currently assert the
pre-conformance behavior (DEVICE_STATE writable via SET_CONFIG, reset value
`0x00` valid, old vendor-info/CAPS byte offsets); writing P5 against
anything other than the final P2+P3+P4 state would produce tests that
immediately fail or, worse, silently pass against the wrong contract. P6
(chardev tests) depends only on P1, since R1/R2 are independent of the Core
GID work. P7 (ASan Makefile target) depends on P6 because the whole point of
the target is to run the AC2 repro test P6 adds — building it before that
test exists would leave the target with nothing meaningful to verify. P8 (the
full-branch review gate) depends on every other step, per the plan format's
gate requirement, and is the only step whose Description explains *why* a
per-commit view is insufficient for this specific set of changes — a bug
introduced by P4's response-format change is invisible to a diff of P4 alone
if the reviewer isn't also looking at P5's updated expectations in the same
pass, and a bug in P2's reset path leaving `device_state` untouched via
`store_config()` is invisible to a diff of P2 alone if the reviewer isn't
also looking at P4's guard in the same pass.
