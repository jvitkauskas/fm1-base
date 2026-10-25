# NimBLE 1.10.0 — fm1-base open BLE

Worktree: `/root/fm1/fm1-base-nimble110`, branch `nimble-1.10`, starting
commit `6af1dedc312d74f8ea4e43ef5057502e4a81e79e`. No commits, flashing,
USB/Bluetooth/device access, or changes to Felucca-ble or the other fm1-base
worktree were made. The excluded `ble/`, `device/radio.c` and `examples/wifi/`
sources were not used. Validation is offline; hardware acceptance remains open.

## Pin, subset and source review

NimBLE is now **1.10.0**, tag `nimble_1_10_0_tag`, commit
`a7a156f28954819e158b62dd613008f22f9cf73b`, replacing 1.9.0 / tag
`nimble_1_9_0_tag` / `da7e3256da3ba80b232df880f40d8359311cc62e`.
Only the supplied, read-only upstream checkouts and allowed fm1-base files were
used. No additional upstream dependency was fetched.

Every existing upstream file was compared: **129 NimBLE files** (20 changed,
109 identical) and **23 TinyCrypt files removed by upstream 1.10**. The latter
remain at their 1.9 pin, including their AUTHORS and BSD/MacKay licenses. No
new upstream host, NPL, transport or service file is required. New dependencies
of the compiled host are its four crypto include names, supplied by the
integration adapter described below. The upstream LICENSE and NOTICE are
copied intact from 1.10; the upstream NOTICE copyright ends in 2026. The old
TinyCrypt license remains in `ext/tinycrypt/LICENSE` even though 1.10's
aggregate LICENSE no longer references that removed directory.

`NIMBLE-1.10-INVENTORY.json` records all 152 paths, old/new/integrated SHA-256
values, unchanged files and removals. `NIMBLE-1.10-UPSTREAM.diff` contains the
20 changed NimBLE files; `NIMBLE-1.10-FM1-HOOKS.patch` contains every difference
from the corresponding upstream pin. Only the five pre-existing hook files
differ. `VENDOR-AUDIT-NIMBLE-1.10.log` records the hash/configuration checks
and a zero-fuzz reverse-hook patch dry run. The original 1.9 hook patch is also preserved for comparison.

### SM, bonding and security

* `ble_sm.c` persists successful bond keys **before** pairing-complete and
  encryption-change callbacks. Applications can now read both security
  directions at pairing completion; identity resolution/store callbacks also
  occur earlier. The store object types, record formats, callbacks, key
  distribution, repeat-pairing decision/retry loop and key retrieval are
  otherwise unchanged. Here persistence invokes the existing store callbacks
  into RAM; journal programming remains deferred until the link is idle. The LTK-request handler also checks that its connection
  exists before creating/restoring a procedure.
* `ble_sm_alg.c` switches AES, CMAC, P-256 key generation and ECDH from
  TinyCrypt to Mbed TLS calls. RNG is passed per operation; the global
  `ble_sm_alg_ecc_init()` hook is removed from `ble_sm_priv.h` and
  `ble_sm_sc_init()`. Its SELFTEST-only constant RNG is not enabled here;
  production still uses LE Rand with the boot-seeded entropy service.
* `ble_sm_lgcy.c` rejects an initiator receiving its own pairing random back
  with Confirm Value Failed, blocking the mirrored-confirm/random path. This
  branch is for the SM initiator; FM-1 remains the responder in peripheral
  pairing. No application retry or fallback was added around that rejection.
* `ble_gatts.c` now makes CCCD **writes** inherit the characteristic's
  encryption/authentication requirements and minimum key size. Authorization
  is deliberately not inherited. Public CCCD reads remain public. FM-1 MIDI
  CCCD writes therefore reject an unencrypted peer until it pairs/encrypts;
  Service Changed's unprotected CCCD can still be subscribed before pairing.
  The existing CCCD handler hook runs after ATT permission checks: these new
  security rejections produce an ATT error, without a handler-write event.
* These are source-observed security changes, not a CVE/advisory inventory.
  Retaining TinyCrypt means this integration does not acquire Mbed TLS's
  implementation or any security properties unique to that backend.

### GAP, privacy and connection parameters

* `ble_hs.c` clears cached host privacy state during host Reset, so resync
  repeats controller privacy setup. `ble_hs_stop.c` releases the host lock
  before HCI reset and GAP teardown, avoiding nested teardown locking, and
  still clears identity/privacy state.
* `ble_gap_update_params()` now falls back to L2CAP signaling when the HCI
  connection-update command returns Unsupported Remote Feature (0x1a) while
  acting as a peripheral. Other errors and the central role do not take this
  new fallback. The FM-1 controller currently advertises no LL Connection
  Parameters Request feature, so the existing direct L2CAP path remains its
  normal path; the new status path is separately exercised with a test wrapper.
* An idle master-failure case is tolerated, and the misspelled deprecated
  `BLE_GAP_EVENT_PARING_COMPLETE` alias is removed. FM-1 uses the correctly
  named API. Central/scanner functionality remains disabled.

### GATT/ATT, L2CAP, transport and store

* ATT Find By Type Value reads the full attribute into a temporary mbuf and
  requires exact value length as well as content. Read By Type replaces its
  19-byte temporary buffer with an mbuf and limits returned values to
  `min(MTU - 4, 253)`. This matters to DIS reads and mixed 16/128-bit service
  discovery. Both temporary buffers are freed on completion/errors.
* Variable Read Multiple checks later handles even when the response has
  filled the MTU, and frees the success buffer before producing an error.
  The GATT client's corresponding parser handles malformed/truncated values
  and allocation failures instead of asserting. These BLE 5.2/client paths
  are disabled in the FM-1's BLE 5.0, peripheral-only profile. The disabled
  server opcode is checked to remain unsupported.
* Deprecated `ble_gattc_notify[_custom]` and `ble_gattc_indicate[_custom]`
  aliases are removed. FM-1 already uses `ble_gatts_*`. Notification-multiple
  loop indexes become `size_t`; that feature remains disabled.
* `ble_att.c`, `ble_l2cap*.c/.h`, `ble_store.c`, `ble_store_util.c`, GAP/GATT
  services, privacy source, transport implementation/headers, NPL interfaces
  and mbuf/mempool sources in the subset are byte-identical between pins.
  Service Changed scheduling/confirmation, per-peer CCCD restore, ATT MTU
  negotiation and L2CAP segmentation/reassembly do not change upstream.
* Host HCI ACL credit accounting now reserves a controller buffer **before**
  transport submission and rolls it back on submission failure, preventing a
  completed-packets race. HCI command locking precedes the pending-ACK assert.
  The advertising-report iterator uses `sizeof(*rpt)` instead of pointer size;
  scanner code is disabled here. HCI version constants add 6.2/6.3 and the ISO
  lost-packet status is corrected to 0x02; ISO remains disabled. No HCI
  transport callback signature or ownership rule changes.
* `ble_hs_conn.c` declares timeout locals only when the associated features
  are enabled. A duplicate semicolon is removed in GAP/HCI declarations.

## Integration choices

The supplied 1.10 tree removes TinyCrypt but has **no Mbed TLS implementation**;
its package file requests an external Mynewt-core crypto package. Within the
allowed sources, the build therefore retains TinyCrypt and adds
`open_ble/ble_sm_crypto.c` plus `open_ble/port/mbedtls/`. This is a **private
source adapter for the calls in the pinned SM file, not Mbed TLS or its general
API/ABI**. The upstream 1.10 SM algorithm file is compiled unchanged.

The adapter supports AES-128 encryption, one-shot CMAC, fixed 32-byte integer
serialization and uncompressed P-256. Other key sizes, modes, curves, formats,
lengths, invalid/off-curve/infinity/noncanonical points and invalid private
scalars fail. It uses fixed storage, wipes its contexts and scalar work, and
scopes/restores TinyCrypt's global RNG with a reentry guard. A valid generator
point is accepted, as required by point validation. Shared-secret
multiplication uses TinyCrypt's existing ECC primitives with explicit random
projective Z: retained `ecc_dh.c` has a separate unset RNG pointer, so simply
calling its wrapper would discard the new per-operation blinding RNG.

LE Rand remains the entropy source; no constant/debug/fallback RNG is used.
The host and test central each consume 32 additional bytes for ECDH blinding.
The integration test's initial SC total is therefore 18 LE Rand calls instead
of 10 (the test central accounts for four of the new calls); legacy remains
6. This is an expected entropy-traffic change, not a relaxed assertion.

`tools/nimble_build.py` compiles the adapter with the existing S3 TinyCrypt
sources for host and target builds; no new external library is linked. The
removed `ble_sm_alg_ecc_init()` declaration/call was removed from `ble_host.c`.
The application privacy-cache reset at local-IRK generation and all repeated
pairing/store replacement callbacks remain. DIS software revision now reads
`NimBLE 1.10.0 / open R3`; handles and database generation remain unchanged.
No NPL changes were required. `tools/build.py` only changes its version text;
its gates and memory limits are unchanged.

The `ecc.c` multiply hook retains exactly its `FM1B_BLE_OPEN` condition. The
four R3 hook files retain exactly `FM1B_BLE_DIAG && FELUCCA_BLE_STAGE >= 3` and
metadata-only arguments. The SM result hook remains before the application
callbacks, now after upstream key persistence. No protocol, permission or
crypto fallback patch was added to an upstream file.

### Settings and defaults

`NIMBLE-1.10-SETTINGS.json` lists **every** added/removed/changed generated
`MYNEWT_VAL` name, old/new default and integration value/reason.
`NIMBLE-1.10-CONFIG.diff` preserves the package/syscfg migration, including
standalone versus package socket defaults and the external crypto dependency. There are
65 new `MBEDTLS_*` definitions and four socket-choice definitions.
`open_ble/port/fm1_nimble110_config.h` explicitly sets all additions:

* `MBEDTLS_AES_C=1`, `MBEDTLS_ECP_DP_SECP256R1=1` describe the supported crypto
  operations. `MBEDTLS_CMAC_C=(FELUCCA_BLE_STAGE >= 3)` matches the new host
  syscfg SC override (generated standalone-header default is 0; SC sets 1).
* `MBEDTLS_MPI_MAX_SIZE=32` records the fixed P-256 serialization bound,
  replacing the generated 1024 default. Every other new Mbed setting is 0:
  no TLS, extra curves, cipher modes, generic MPI, hashes, entropy provider or
  platform alternatives are linked. Upstream nonzero defaults additionally
  include BASE64, ENTROPY, PKCS1 V15/V21 and SHA256; they are unused here. The
  existing LL CCM/AES and seed/privacy SHA-256 backends remain in use directly.
* `BLE_SOCK_TYPE` and all three choices are explicitly 0, because the existing
  transport is in-process/custom. The new upstream default choice is
  `linux_blue=1`, with `linux_tcp=0` and `nuttx=0`, because the porting-default
  target selects linux_blue. The socket package's own YAML default is
  linux_tcp. The old `BLE_SOCK_USE_TCP`, `BLE_SOCK_USE_LINUX_BLUE` and
  `BLE_SOCK_USE_NUTTX` switches are deprecated and map to the new choice when
  true. TCP's package default changes 1 -> 0; its generated header was
  already 0. `BLE_SOCK_USE_LINUX_BLUE` changes generated default 1 -> 0 and
  is explicitly 0 here too; its package default remains 0. NuttX remains 0.
* Removed TinyCrypt generated settings are SYSINIT_STAGE=200,
  UECC_RNG_TRNG_DEV_NAME="trng", UECC_RNG_USE_TRNG=0; retained files do not use
  them. Defunct transport `#undef` aliases and NATIVE_SOCKETS_POLL_ITVL are
  removed; the integration already uses the modern `BLE_TRANSPORT_*` names.
* `BLE_VERSION` accepts 62/63 in addition to previous values; its default
  remains 50. No new host-role, bonding, ATT MTU, connection-parameter, pool or
  store limit was introduced. Existing explicit limits remain in
  `felucca_nimble_config.h`.

## Validation

Final full sanitizer matrix: **PASS**, aggregate process exit 0 and
`All open BLE ASan/UBSan suites passed.` in `TESTS-NIMBLE-1.10-ALL.log`.
No ASan/UBSan error or leak was reported.

The full entry point is `python3 tests/run_open_ble_tests.py`, with its default
10,000,000-event stress counts, ASan and UBSan enabled, leak detection and
halt-on-error. It includes S1/S2/S3/S3.1, object-store and diagnostics-off
profiles, F-28/F-29, encrypted connection updates, `--mic-stress`,
`--s31 --open-store --reliability`, and `--mic-stress --reliability`.
`python3 tests/open_ble_build_test.py` passes all four Python build-policy
tests; it checks policy, not a sanitizer-instrumented firmware binary.

Completed S3 profiles (each uses ASan/UBSan):

| Runner arguments | Result | Extracted log suffix under `TESTS-NIMBLE-1.10-` |
|---|---|---|
| none (S3) | PASS | `S3.log` |
| `--s31` | PASS | `S31.log` |
| `--s31 --open-store` (F-28/F-29 included) | PASS | `STORE.log` |
| `--s31 --open-store --no-diag` | PASS | `RELEASE.log` |
| `--mic-stress` (full counts) | PASS | `MIC.log` |
| `--s31 --open-store --reliability` (R7 host, F-28/F-29) | PASS | `RELIABILITY-HOST.log` |
| `--mic-stress --reliability` (full counts + 22 directed cases) | PASS | `RELIABILITY-MIC.log` |

`TESTS-NIMBLE-1.10-RESULTS.json` indexes the successful aggregate, profiles,
commands, logs and counts. The aggregate also passes the original seam, MIDI,
health/advertising, connection-update/soak, S1 and S2 suites.

Both stress profiles complete **5,000,000 ping events at 15 ms + 5,000,000
at 30 ms + 10,000,000 inherited stress events**: 40 million total modeled
events across the two profiles. Their ping results match R7 exactly: 705,930
requests / 705,929 replies / 356,540 writes at 15 ms; 984,008 requests/replies
and 335,279 writes at 30 ms. The single pending 15-ms reply at the finite run
boundary matches R7.

The ordinary stress profile authenticates 2,280,220 RX PDUs, with 256,465
repeats/lost ACKs, 126,643 CRC errors, 48,073 misses and 2,333 updates. R7
reliability authenticates 1,513,033 RX PDUs, with 229,713 repeats/lost ACKs,
126,750 CRC errors, 48,320 misses, 2,269 updates and 2,938,005 pinned-slot
refusals. These match the respective R7 baselines. Both have zero spontaneous
MIC, late-instant and supervision failures. The 22 directed R7 cases pass,
including counter/direction, prior key/IV, DMA/copy corruption, AES faults,
key/derivation disagreement, TX suppression, phase, timing/audio, stale command,
automatic-empty toggle and KAT fairness. Maximum modeled diagnostic item
remains 2,168 ticks = 90.33 us, with no item at/above 100 us. These are model
results, with the same hardware limits as R7.

New tests run against the actual vendored host:

* `ble_nimble110_crypto.c` and the Apache-licensed upstream vector cases test
  AES/f4/f5/f6, P-256 generator/known 2G, byte order, independent blinding Z,
  invalid points/scalars/formats, RNG failure/restoration, unsupported inputs
  and context erasure. No sanitizer exclusion is added.
* `ble_nimble110_gap.c` substitutes only the HCI update command status, then
  checks the real host's L2CAP request, rejection/cleanup, other errors and
  central-role exclusion. Other HCI/LL traffic uses the real integration.
* `ble_nimble110_test.inc` checks unencrypted MIDI CCCD rejection with public
  reads, long Read By Type, exact Find By Type Value, MTU 517, and both stored
  security directions being visible during pairing completion. Existing
  suites continue to check encrypted MIDI both ways, legacy/SC, repeated
  pairing with same/changed IRKs and unresolved RPAs, bonded reconnect,
  Service Changed loss/retry/confirmation, CCCD restore and encrypted updates.

An initial full run failed F-29's *test-only* historical event-mask trace
lookup: extra RNG traffic evicted startup HCI 0x2001 from the bounded ring.
The test now verifies the mask immediately once per host sync; every call
still checks live privacy enablement/count. No production trace capacity,
privacy logic or acceptance condition was relaxed. The focused F-29 rerun
passes (`TESTS-NIMBLE-1.10-F29.log`); the initial run is preserved in
`TESTS-NIMBLE-1.10-INITIAL.log`. The inherited TinyCrypt AES-only
`-fno-sanitize=shift-base` remains; no other sanitizer suppression was added.

## Target images and comparison with R7

Both final target configurations pass source-only link, DMA/MMIO boundary,
RAM-code/no-flash-call, multiply/load-erratum, no-soft-double, RAM and pool
headroom gates. No gate was changed or bypassed. RAMTEXT remains 3,112 bytes
(966 instructions in the checked RAM section); trace reservation is 20,480
bytes for diagnostics and zero for release. Loader image is 8,264 bytes and
packed OTA loader is 6,980 bytes.

| Profile | App image | Delta R7 | .data + .bss / 98,304 | Delta R7 | Pool / 344,064 | Delta R7 | FWSC |
|---|---:|---:|---:|---:|---:|---:|---:|
| Diagnostics on | 208,912 | +1,520 | 34,644 | 0 | 154,428 | +16 | 610,136 |
| Diagnostics off | 188,224 | +1,520 | 33,760 | 0 | 99,436 | 0 | 610,136 |

R7 baseline: on 207,392 / 34,644 / 154,412; off
186,704 / 33,760 / 99,436 (image / .data+.bss / pool), from `RESULTS-R7.md`.
FWSC size is unchanged. The new package names do not overwrite saved R7
packages. Each new image has matching saved `.bin`, `.map` and `.symbols`.

* `template/build/template-open-nimble110.fwsc` — SHA-256
  `870e39f2617a132fc12d9c85d6b9cbe9c5fa7b982fe6f71005959734c8f627b3`.
* `template/build/template-open-nimble110-release.fwsc` — SHA-256
  `96ffbbc2e76be42827dbf8b6d05b30de03d6e4bbf1b8a4d800448be6f2a4d920`.

Hashes are also in `SHA256-NIMBLE-1.10.txt`; source and artifact fingerprints
are in `nimble110-source-record.json`. `DIAGNOSTICS-OFF-NIMBLE-1.10.txt`
checks exclusion of the event hooks and R7 state/worker/KAT/DMA record symbols
from release. Build logs are
`BUILD-NIMBLE-1.10-ON.log`, `BUILD-NIMBLE-1.10-OFF.log`; policy output is
`BUILD-POLICY-NIMBLE-1.10.log`. Target builds used:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble --ble-diag on
# Repeat with --ble-diag off; copy each result to its saved name above.
```

## Hardware risks and owner bench (not run)

The R7 encrypted-link drops were traced to the desktop's Intel AX210 sending
wrong MICs (`BENCH-R7.txt`, added after this report). This host update does
not change that path. Host simulation cannot establish
silicon AES timing, interrupt/audio behavior, actual RF reliability, or peer
OS interoperability. The private crypto adapter retains the old ECC backend;
vector tests establish functional results, not a side-channel/security audit
or equivalence to the upstream Mbed TLS implementation. Its extra entropy
work and stack use need observation during SC pairing on target.

Check that clients retry a protected CCCD write after initiating security,
that earlier key persistence does not expose a partially replaced bond to an
owner action, and that RPA privacy setup repeats after Reset/power-cycle.
Observe ATT discovery memory pressure with MTU 517 and connection-parameter
requests from real peers. The normal pool budget gates pass, but this is not
a silicon peak-stack or long-duration audio measurement.

1. With the diagnostics image, pair separately with BlueZ, a Mac and an
   iPhone. Record host/OS version and actual legacy/SC method. Confirm full
   GAP/GATT/MIDI/DIS discovery, readable revision 1.10.0, protected MIDI read,
   pairing, CCCD enable and absence of unsolicited Service Changed for a new
   bond. On one host, forget only its bond and re-pair to exercise replacement.
2. Disconnect once and wait for the pending bond-journal flush to finish,
   then power-cycle FM-1 with bonds retained. Reconnect each host (including a fresh
   RPA where used), confirm encryption with no fresh pairing prompt and restored
   MIDI subscription. Keep another peer's bond while doing this. With a stale
   client cache, check one Service Changed indication and durable confirmation.
3. Send notes, control/program changes and a SysEx transfer in both directions.
   Check timestamps, no missing/stuck notes, negotiated MTU, and the requested
   connection interval/update outcome. Repeat the short check with diagnostics
   off so instrumentation is not a prerequisite for correct operation.
4. Run a **30-minute** bidirectional MIDI soak with normal USB audio load.
   Record reconnects, MIC/supervision failures, MIDI drops, audio late/xruns,
   executor over-100-us counts and R7 frozen-record completeness. Require zero
   unexpected drops/failures and audio/timing overruns; preserve any frozen
   diagnostics and host logs. Do not infer a repaired physical link from the
   offline results or silently retry away a failure.
