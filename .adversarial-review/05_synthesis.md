## Synthesis Report

### Summary

Both reviewers independently reached a `REQUEST_CHANGES` verdict, agreeing that the diff regresses two previously-enforced invariants: (1) the packet serializer used to fragment oversized control payloads but now silently truncates them, breaking the UCI wire contract, and (2) profile-level app-config validation has been gutted. The reviews are highly convergent on transport-layer regressions (serializer truncation, lack of PBF fragmentation in `send_to_client`, oversized inbound desync, SIGTERM/SIGPIPE/global-state issues in the new poll-based server) and on the QORVO_MAC handler being undersized-on-output and undervalidated-on-input. The main disagreements are about exploitability (A1 as a CWE-200 leak vs. a length-inconsistency bug), reachability (B3's uint8_t hang), and severity weighting (A5/A10). Cross-review also surfaced a previously missed array-overflow in the default profile (81 entries written into an 80-slot array) and a SIGTERM-swallowed-in-recv shutdown bug.

### Cross-Validated (🟢) — both reviewers independently found the same issue

| # | Issue | A's id | B's id | Site |
|---|---|---|---|---|
| 1 | Serializer silently truncates control payloads >255B and returns success | A2 | B1 | `uci_sim_packet.c:53` |
| 2 | TCP transport no longer PBF-fragments oversized control responses (couples with #1) | (A2) | B2 | `uci_sim_tcp_server.c:50` |
| 3 | `uci_sim_profile_supports_session_app_config()` now unconditionally returns 1, breaking profile-driven shaping | A4 | B6 | `uci_sim_profile.c:561` |
| 4 | `CORE_DEVICE_INFO` advertises more vendor bytes than the packet actually carries when `vendor_specific_length > 245` | A1 (length aspect) / ADD3 | B9 | `uci_sim_handlers.c:394` |

Note on #4: B challenged A1's framing as a *CWE-200 information leak* (since `init_result()` memsets the response buffer before each dispatch — see Disputed below) but B independently filed the same code site as a *length-inconsistency* bug (B9, then re-stated as ADD3 in B's cross-review). The underlying defect — a malformed DEVICE_INFO response — is agreed.

### Consensus (🟡) — found by one reviewer, validated by the other in cross-review

**From Claude (A), validated by Codex (B):**

- **A3 — QORVO_MAC `GET_CALIBRATIONS` response can exceed 255 bytes** (`uci_sim_handlers.c:330`). Echoes variable-length keys, only capped against `UCI_SIM_MAX_PAYLOAD`. Couples with the serializer truncation to produce a malformed control packet whose `num_returned` does not match the entries actually serialized.
- **A6 — SIGTERM handler + `g_shutdown` global make the server non-reentrant** (`uci_sim_tcp_server.c:88`). Prior `sigaction` not saved/restored; two concurrent calls would race; `g_shutdown` is not reset at serve start (so a later call inherits stale state).
- **A7 — Errors on `listen_fd` not surfaced** (`uci_sim_tcp_server.c:196`). Loop never inspects `pfd[1].revents` for `POLLERR | POLLHUP | POLLNVAL`.
- **A8 — Return values of `setsockopt` and `sigaction` ignored** (`uci_sim_tcp_server.c:145`).
- **A9 — QORVO_MAC `.port` parser uses brittle magic position-3 byte read** (`uci_sim_handlers.c:260`). Any key ending in `.port` that isn't shape `antX.port` silently decodes the wrong antenna index.

**From Codex (B), validated by Claude (A):**

- **B4 — Oversized inbound DATA payload desynchronizes the TCP stream** (`uci_sim_tcp_server.c:113`). 16-bit declared length clamped to `sizeof(pkt->payload)`; the remaining declared bytes stay in the kernel socket buffer and are misparsed as the next header.
- **B5 — Simulated time advances per loop iteration, not by elapsed time** (`uci_sim_tcp_server.c:196`). `uci_sim_engine_tick(engine, 50)` fires unconditionally per `poll()` wake; bursts of commands accelerate virtual time, causing scheduled notifications to fire ahead of cadence.
- **B7 — `send()` can kill the process with SIGPIPE** (`uci_sim_tcp_server.c:56`). No `MSG_NOSIGNAL`, no `SIG_IGN` on SIGPIPE; peer disconnect during a notification flush can terminate the simulator, defeating SIGTERM-based test teardown.
- **B8 — Truncated/malformed `GET_CALIBRATIONS` returns a partial `UCI_STATUS_OK`** (`uci_sim_handlers.c:291`) instead of `UCI_STATUS_INVALID_MSG_SIZE`.

### Disputed (🔴) — disagreement, unresolved

- **A1 — `CORE_DEVICE_INFO` info-leak framing** (`uci_sim_handlers.c:405`).
  - **A's position:** Bytes between offset 10 and `payload_len` can come from a previously-occupied response buffer (CWE-200).
  - **B's position:** Not reachable. `uci_sim_device_handle_packet()` calls `init_result()` which memsets the whole `result` before dispatch, so any skipped copy region holds zeros, not stale data. The genuine bug at this site is the length inconsistency (cross-validated above), not a leak.

- **A4 — supporting claim about the supported-IDs array being "dead"** (`uci_sim_profile.c:561`).
  - **A's position:** `profile->supported_session_app_config_ids[]` is now dead/never consulted, a stale-data hazard.
  - **B's position:** The core concern is real; but the array is *not* dead — `GET_CONFIG` with `count == 0` still iterates it. The sharper issue is that the supports_* API now disagrees with the GET_CONFIG path that still treats the list as authoritative.

- **A5 — Second-client connections accepted-and-closed** (`uci_sim_tcp_server.c:175`). Also re-raised in A's own additions as A_ADD2.
  - **A's position:** Major. Backlog raised to 4, no log, silent half-connections mask connectivity bugs.
  - **B's position:** Behavior is accurately described, but severity is overstated — the header and source both document a one-client server. A log line would help; this isn't a major correctness defect.

- **A10 — `device_stats_temperature` duplicated on profile and device** (`uci_sim_profile.h:31`).
  - **A's position:** Divergence hazard; pick one.
  - **B's position:** Normal profile-default-to-runtime-state copy pattern, also used for versions/device defaults. Not a defect; a rename to `default_*` could improve clarity.

- **B3 — 255-byte calibration key can hang the handler** (`uci_sim_handlers.c:299`).
  - **B's position:** `uint8_t jndex` iterating up to `key_len` wraps if `key_len == 255`, hanging the loop.
  - **A's position (definitive challenge):** No hang. At `jndex=254` the body runs, `jndex++` → 255 (fits in `uint8_t`), the test `255 < key_len` (where `key_len ≤ 255`) is false, loop exits. **Resolution: B3 is not a real bug.**

### New Findings from Cross-Review

**Added by Codex (B) when reviewing A:**

- **ADD1 — Default profile overflows the supported app-config ID list** (`uci_sim_profile.c`). 🔥 **Major.** Profile initializes 81 `supported_session_app_config_ids` while `UCI_SIM_MAX_PROFILE_FEATURES` is 80. GCC emits an excess-initializer warning and drops the final `0xE7` entry, but `supported_session_app_config_id_count` is still set to 81. `GET_CONFIG` with `count == 0` reads one byte past the array (likely into the count field), and `TX_ANTENNA_SELECTION` is not enumerated.
- **ADD2** — duplicates B4 (oversized inbound DATA desync). Already cross-validated above.
- **ADD3** — duplicates B9 (DEVICE_INFO length inconsistency). Already cross-validated above.

**Added by Claude (A) when reviewing B:**

- **A_ADD1 — SIGTERM is swallowed while blocked in `recv`** (`uci_sim_tcp_server.c:81-87, 102-109`). **Major.** Handler installed without `SA_RESTART`, but `read_command` explicitly retries on `EINTR`. `g_shutdown` is consulted only between loop iterations, so a server blocked in `recv` cannot terminate until the client sends or closes something. Breaks the documented SIGTERM-for-teardown contract.
- **A_ADD2** — duplicates A5 (second-client silent drop). Already in A5; disputed above.
- **A_ADD3 — Flush after `engine_tick` can attempt `send` on a half-closed peer** (`uci_sim_tcp_server.c:210-216`). `POLLERR/POLLHUP/POLLNVAL` are checked *after* the unconditional `engine_tick`/`flush_to_client`. If the peer half-closed, the write hits a FIN'd socket; combined with B7's missing SIGPIPE handling, this is a realistic crash trigger on graceful disconnect.

### Overall Verdict

**REQUEST_CHANGES** (both reviewers agree).

The diff delivers useful functionality (DEVICE_INFO vendor blob, Qorvo proprietary GIDs, `select → poll` modernization) but introduces a cluster of transport-layer regressions whose cumulative effect is that large or adversarial UCI traffic can no longer be trusted on the wire:

- Control responses >255 bytes are silently truncated to a malformed packet (🟢 cross-validated).
- Oversized inbound DATA packets desynchronize the TCP stream (🟡 consensus).
- Simulated time advances per loop iteration rather than elapsed wall time, distorting scheduled notifications (🟡 consensus).
- The TCP server can be killed by SIGPIPE during a flush, or fail to shut down on SIGTERM while blocked in `recv` (🟡 consensus + 🟡 new finding).
- Profile-level app-config validation is gone (🟢 cross-validated), and an off-by-one in the default profile silently overflows the supported-IDs array (🟡 new finding from cross-review).

### Recommended Fix Priority

**P0 — must fix before merge (correctness / crash / data-corruption on the wire):**

1. Restore PBF fragmentation for oversized control responses; make the serializer return an error rather than silently truncate. *(A2 / B1 / B2)*
2. Reject or drain-and-close on oversized inbound DATA packets; do not submit a truncated payload upstream. *(B4 / ADD2)*
3. Fix the default profile's 81-entry initializer into an 80-slot array (and the resulting out-of-bounds `GET_CONFIG count==0` read). *(ADD1)*
4. Make `send_to_client` survive peer disconnect: use `MSG_NOSIGNAL` and/or `SIG_IGN` for SIGPIPE. *(B7)*
5. Honor SIGTERM while blocked in `recv`: check `g_shutdown` in the `EINTR` branch (or stop swallowing EINTR). *(A_ADD1)*
6. Check `POLLERR/POLLHUP/POLLNVAL` *before* writing to the peer in the post-tick flush path. *(A_ADD3)*

**P1 — should fix in this PR (correctness, but lower impact):**

7. Drive `uci_sim_engine_tick` from elapsed monotonic time, not per-iteration. *(B5)*
8. Reconcile `uci_sim_profile_supports_session_app_config()` with the `GET_CONFIG count==0` path that still iterates `supported_session_app_config_ids[]` — either honor the list or remove it (with a per-profile `accept_all_app_configs` flag if permissive storage is intentional). *(A4 / B6)*
9. Make `CORE_DEVICE_INFO` self-consistent: validate `vendor_specific_length ≤ 245` so the byte at `payload[9]` matches what is actually sent. *(A1 length aspect / B9 / ADD3)*
10. Cap `GET_CALIBRATIONS` response building at 255 bytes (control GID), or fragment; return only the entries that fit and write the actual `num_returned`. *(A3)*
11. Return `UCI_STATUS_INVALID_MSG_SIZE` (not partial OK) on malformed `GET_CALIBRATIONS` input. *(B8)*

**P2 — cleanup / observability (non-blocking):**

12. Save/restore prior SIGTERM disposition; reset `g_shutdown` at serve entry; document the SIGTERM hijack in the header. *(A6)*
13. Check `setsockopt` and `sigaction` return values. *(A8)*
14. Check `POLLERR/POLLHUP/POLLNVAL` on `listen_fd`. *(A7)*
15. Make the QORVO_MAC `.port` parser anchor on `ant` and reject non-conforming keys instead of silently returning 0. *(A9)*
16. Decide on second-client policy: either don't accept while a client is connected, or `shutdown(SHUT_WR)` + log before close so the peer gets a deterministic signal. *(A5 / A_ADD2)*
17. Rename/consolidate `device_stats_temperature` (e.g., `default_device_stats_temperature` on profile) for clarity. *(A10)* — optional.

**Close as not-a-bug:**

- **B3** — the `uint8_t` loop terminates correctly; no hang is reachable. A's challenge is definitive.
