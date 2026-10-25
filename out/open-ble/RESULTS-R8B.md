# R8B release candidate — idle-only persistence

Base: `/root/fm1/fm1-base-open`, branch `open-ble`, HEAD `fa1431b` (R7 plus
Apache NimBLE 1.10.0). Selected changes were read from `r8-sram-store` at
`c1633e6`. All R8B work is uncommitted. No device was contacted or flashed.
Only the frozen r8 specification, permitted fm1-base source/history and
Felucca-ble source were used; excluded `ble/`, `device/radio.c`, and
`examples/wifi/` sources were not used. Toolchain/SDK package inputs are the
same as the earlier candidate builds; no new dependency was fetched.

**Validation: PASS.** Both target builds, all 15 base-platform test groups,
the complete fm1-base ASan/UBSan matrix (40 million modeled stress events and
22 directed R7 cases), and all five Felucca BLE suites (20 million modeled
stress events) pass. Counts were not reduced. Physical pairing, timing, audio
and long-soak acceptance remain for the owner; see the single
[OWNER-BENCH-R8B.md](OWNER-BENCH-R8B.md).

## Included changes

- `include/fastmath.h`: `fm_exp2_nf` adjusts exponent bits using an unsigned
  shift, avoiding undefined behavior for negative exponents. Existing full
  fastmath accuracy sweeps and the host template/audio exercise run under
  UBSan without an exemption.
- `device/mirror.c`: stop speculating a second row when the first compressed
  row has already exceeded `MR_CAP`. The scratch buffer holds the cap plus
  one row. Existing busy/noisy-screen, decoder/viewer and PNG tests run under
  ASan/UBSan and validate the resulting stream.
- `device/storage.c`: header/payload read I/O errors return a distinct
  internal `-2`, separate from blank or invalid copies (`-1`). If selecting
  the current A/B copy encounters I/O, `st_save` returns before any erase or
  program. CRC-invalid copies can still fall back to a valid copy, and blank
  or fully invalid stores can still be initialized. Readback verification,
  on-flash format, header-last commits and the idle-only contract are unchanged.
- `tests/ldr_test.c`: free the two loader-test package buffers. The R8
  `PKG` environment override in `tests/run_tests.sh` is retained so the OTA/
  loader tests explicitly consume this candidate's release package.
- An additional **test-only** cleanup frees `raw` and `logical` in
  `tests/ota_test.c`. The first R8B base run exposed the pre-existing raw-buffer
  leak under LeakSanitizer; the final complete base run passes with leak
  detection enabled. Evidence: `TESTS-R8B-BASE-INITIAL.log` and final
  `TESTS-R8B-BASE.log`. No OTA firmware code changed.
- New read-fault regressions cover either header, the winning payload and a
  CRC-invalid winner's fallback payload, in both A/B directions. They assert
  distinct I/O classification, zero erase/program calls and byte-identical
  sectors. The real FMOB/bond suite also checks that failures leave pending
  state intact, that linked flushes remain refused, and that an idle retry
  succeeds and reloads the new key. Blank/invalid recovery and all inherited
  migration, downgrade, corruption and power-cut tests remain enabled.
- README and `docs/OPEN-BLE.md` explain the first-pair power-off limitation,
  `bond_pending 1`, the disconnect-once workaround, and R4→R5+ migration /
  R5+→R4 starting with no bonds. The current CCCD section now describes
  NimBLE 1.10's inherited encrypted-write requirement instead of the old
  1.9 behavior. This requirement was already present in `fa1431b`; R8B makes
  no additional security/host change.

## Deliberately excluded

No SRAM runtime, `device/app_open_ram.ld`, bootstrap/linker relocation,
connected-write gate, live-save deadlines/counters, flash-wait servicing, or
Felucca-BSP flash-driver change was ported. The controller, host, platform,
`tools/build.py`, existing linker scripts and BSP submodule are unchanged
from `fa1431b`. No change is made for R7's `0x3D` drops: the owner's
`BENCH-R7.txt` traced those to the desktop Intel AX210 sending wrong MICs.

The opt-in `r8-sram-store` branch remains available for apps that accept its
cost: all runtime code/constants in SRAM, a 208 KiB runtime limit, 40 KiB
static region, and pool capacity **188,416 rather than 344,064 bytes**
(184 rather than 336 KiB). It also needs the branch's documented BSP patch.
The owner declined to merge that approach. R8B keeps the original XIP layout
and the full 344,064-byte pool capacity. R8's connected-save/flash-timing
harnesses and XIP-runtime rejection test are not ported: they exercise that
excluded mechanism. The current aggregate, existing build policy and new
idle-only read-fault regressions are the candidate's relevant suites.

## Persistence contract for this candidate

A new bond or MIDI subscription change remains in RAM until the BLE link
closes. Powering off while still connected after the first pairing loses the
bond, and the central then fails to reconnect until it forgets the FM-1 and
pairs again. After pairing, disconnect once, wait over USB for
`bond_pending 0`, then reconnect before switching off the FM-1. On macOS,
Audio MIDI Setup's “Disconnect” does not close the link: turn Bluetooth off
and on instead. On iOS, toggle Bluetooth off and on, waiting for the pending
flag to clear while it is off. `bond_pending 1` means a save is still waiting.
The same step saves a changed MIDI subscription. Later recency/cache changes
can set pending again even after the original bond is durable.

R4→R5+ migrates existing bonds (retained synthetic R4 fixture and actual R4
parser tests). R5+→R4 starts with no bonds: the R4 parser rejects the truncated
FMOB v2 object; the central must forget and pair again. Neither R8B nor these
notes change the on-flash format.

## Firmware artifacts and comparison

Build commands (from this source tree):

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble --ble-diag on \
  --package-name template-open-r8b.fwsc

JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble --ble-diag off \
  --package-name template-open-r8b-release.fwsc
```

| Build | App bytes | Delta vs R7 | Delta vs NimBLE 1.10 | Static .data+.bss / 98,304 | Pool use / 344,064 | Trace bytes | FWSC bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| R7 diagnostics | 207,392 | — | — | 34,644 | 154,412 | 20,480 | 610,136 |
| NimBLE 1.10 diagnostics | 208,912 | +1,520 | — | 34,644 | 154,428 | 20,480 | 610,136 |
| **R8B diagnostics** | **208,944** | **+1,552** | **+32** | **34,644** | **154,428** | **20,480** | **610,136** |
| R7 diagnostics off | 186,704 | — | — | 33,760 | 99,436 | 0 | 610,136 |
| NimBLE 1.10 diagnostics off | 188,224 | +1,520 | — | 33,760 | 99,436 | 0 | 610,136 |
| **R8B release** | **188,256** | **+1,552** | **+32** | **33,760** | **99,436** | **0** | **610,136** |

Both builds retain RAMTEXT code 3,112 bytes (966 checked instructions), loader
image 8,264 bytes and packed OTA loader 6,980 bytes. Static RAM and pool use
are unchanged from NimBLE 1.10; package sizes are unchanged from both baselines.
Both pass source-only linkage, DMA placement, MMIO ownership, RAM-code/no-call,
multiply/load erratum, no-soft-double and memory headroom gates. All four
existing build-policy tests pass (`BUILD-POLICY-R8B.log`); no gate is bypassed.

Package SHA-256 values:

```text
24caf769a4508f72e747d57a409eef70aba4d9ff8669aedcb134b7aa602b6ad4  template/build/template-open-r8b.fwsc
1b62ac06c7523ee9883ebc1ab23a2815cc57d2b00d934f64f572395f3ac09b8a  template/build/template-open-r8b-release.fwsc
```

`SHA256-R8B.txt` verifies both packages. Matching `.bin`, `.elf`, `.map`,
`.symbols` and `.dis` files are saved under each candidate's basename, so a
later profile build does not destroy its evidence. Logs: `BUILD-R8B-ON.log`
and `BUILD-R8B-OFF.log`.

Baseline package hashes, all 610,136 bytes:

```text
b829afc4cd2d3980fa3e1a73c63393ce891a97fa04a54c89c447ea601599ffea  template-open-r7.fwsc
049117a52063e83c92461e97fc47e329615c9b3f7faf616c287b4ef26e6d134e  template-open-r7-diag-off.fwsc
870e39f2617a132fc12d9c85d6b9cbe9c5fa7b982fe6f71005959734c8f627b3  template-open-nimble110.fwsc
96ffbbc2e76be42827dbf8b6d05b30de03d6e4bbf1b8a4d800448be6f2a4d920  template-open-nimble110-release.fwsc
```

R7 packages were rehashed here and agree with `SHA256-R7.txt`. NimBLE 1.10
package binaries are absent from this worktree; their sizes/hashes are the
recorded values in `RESULTS-NIMBLE-1.10.md` and `SHA256-NIMBLE-1.10.txt`, not
new measurements of those missing files. `PACKAGES-R8B.json` and
`PRIOR-PACKAGES-R8B.txt` preserve that distinction.

`DIAGNOSTICS-OFF-R8B.txt` verifies removal of reliability state/worker/KAT/DMA,
event hooks and R7 record strings from release, while both profiles retain
`bond_pending`, `bond_failures`, identity/capacity and MIDI counters. Detailed
`bond_writes`/`bond_erases`, method/key-size rows and R7/event journals are
available in diagnostics, not release; the owner bench uses the correct
readout for each.

## Host validation

Every C suite uses ASan/UBSan, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Python/JS tooling tests
are also run; they are not C sanitizer binaries. The inherited TinyCrypt
`aes_encrypt.c`-only `-fno-sanitize=shift-base` exemption remains unchanged;
no controller, host-adapter, store, platform or test suppression was added.

fm1-base aggregate command:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 tests/run_open_ble_tests.py > out/open-ble/TESTS-R8B-ALL.log 2>&1
```

The entry point covers seam/MIDI/ranges, diagnostics-on/off health and
advertising seams, offline connection-bench/soak tools, both S1 power variants,
S2, S3, S3.1, FMOB store diagnostics on/off, both R7 profiles, and all NimBLE
1.10 crypto/GAP/ATT regressions. F-28 and F-29 use real host SMP/HCI/CCM.

The aggregate exits 0 and ends `All open BLE ASan/UBSan suites passed.`
`TESTS-R8B-RESULTS.json` indexes each completed profile. The separate profile
logs are extracted from this single final full-count run, not additional
short runs:

| S3 runner arguments | Result | Evidence |
|---|---|---|
| none | PASS | `TESTS-R8B-S3.log` |
| `--s31` | PASS | `TESTS-R8B-S31.log` |
| `--s31 --open-store` | PASS, including F-28/F-29 | `TESTS-R8B-STORE.log` |
| `--s31 --open-store --no-diag` | PASS, including F-28/F-29 | `TESTS-R8B-RELEASE.log` |
| `--mic-stress` | PASS, full counts | `TESTS-R8B-MIC.log` |
| `--s31 --open-store --reliability` | PASS, R7 host and F-28/F-29 | `TESTS-R8B-RELIABILITY-HOST.log` |
| `--mic-stress --reliability` | PASS, full counts plus 22 directed cases | `TESTS-R8B-RELIABILITY-MIC.log` |

Each stress profile completes **5,000,000 ping events at 15 ms + 5,000,000
at 30 ms + 10,000,000 inherited randomized events**, totaling 40 million
modeled events in fm1-base. At 15 ms the ping model observes 705,930 requests,
705,929 replies and 356,540 ATT writes; one reply is in flight at the finite
boundary. At 30 ms it observes 984,008 requests/replies and 335,279 writes.
These match the R7 and NimBLE 1.10 baselines.

The standard stress authenticates 2,280,220 RX PDUs, with 256,465 repeats/lost
ACKs, 126,643 CRC errors, 48,073 deliberate misses and 2,333 updates. R7
reliability authenticates 1,513,033 RX PDUs, with 229,713 repeats/lost ACKs,
126,750 CRC errors, 48,320 deliberate misses, 2,269 updates and 2,938,005
pinned-slot refusals. Both have zero spontaneous MIC, late-instant and
supervision failures. All 22 directed R7 cases pass, covering counters,
direction, prior key/context, DMA/copy/AES/key/derivation/TX faults, phase,
audio preemption, stale command, automatic-empty publication and KAT fairness.
The maximum modeled diagnostic item remains 2,168 ticks (90.33 us), with no
item at or above 100 us. These are model results, not hardware timings.

The complete base-platform suite from `TESTS-R8-BASE.log` passes (15 groups,
no skips): encoder, TRS, storage, fastmath, installer, backup, backup tool,
backup web page, OTA entry, update loader, host build, mirror, mirror viewer,
mirror PNG and host template. It uses this release package for both update
paths. Command:

```sh
CC=/root/fm1/fm1-base-open/out/open-ble/cc-asan-r8b \
AC79_SDK=/root/sdk-blemidi PKG=template/build/template-open-r8b-release.fwsc \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
sh tests/run_tests.sh > out/open-ble/TESTS-R8B-BASE.log 2>&1
```

`cc-asan-r8b` applies ASan/UBSan, frame pointers and non-PIE before the caller's
arguments. `ASAN-R8B-BASE.txt` records the flags and verifies ASan linkage for
the eight C test executables plus the host template. The existing full tests,
not reduced iterations, exercise the fastmath and mirror fixes. All object
profiles retain 10,730 seed/bond byte cuts, 5,365 migration cuts, 5,365 full
8-peer replacement cuts, and 1,236 corruptions plus 1,236 truncations.

## Felucca-ble

The pre-existing R8 idle-save retry fix and its documentation/logs are kept.
Its `ble_host_worker` retains `bond_flush_due` from the actual pending flag
after a failed idle flush. Its journal and idle-only gate are unchanged. The
documentation now calls the SRAM runtime an opt-in fm1-base branch, accurately
reflecting the owner's choice. A new regression in `ble_s3_integration.c`
injects a failure on the first post-disconnect save, asserts pending/no writes,
then removes the fault and proves the real host worker saves without another
disconnect or store mutation; the subsequent persistent-LTK reconnect still
passes. It runs in legacy, SC, RPA and F-28 profiles.

Evidence lives in `/root/fm1/Felucca-ble/out/TESTS-R8B-S1.log`,
`TESTS-R8B-S2.log`, `TESTS-R8B-S3.log`, `TESTS-R8B-S31.log` and
`TESTS-R8B-MIC-10M.log`. All five suites pass. Felucca completes 5,000,000
ping events at 15 ms, 5,000,000 at 30 ms and 10,000,000 inherited stress
events, authenticating 2,280,220 encrypted RX PDUs with zero spontaneous MIC
failures. Commands, from Felucca-ble, with the same
ASAN/UBSAN options and `CC=/root/fm1/Felucca-ble/out/cc-asan-r8b`:

```sh
sh tests/run_ble_s1_tests.sh
python3 tests/run_ble_s2_tests.py
python3 tests/run_ble_s3_tests.py
python3 tests/run_ble_s3_tests.py --s31
python3 tests/run_ble_s3_tests.py --mic-stress
```

An initial R8B Felucca sanitizer attempt re-enabled TinyCrypt's inherited
shift-base check because the temporary compiler wrapper put sanitizer flags
after the runner's per-file exemption. Correcting wrapper flag ordering
retains the existing AES-only exemption; it changes no source or runner
suppression. The final S3/S3.1 logs use the corrected wrapper and include the
new retry regression. The earlier R8 evidence logs are preserved.

## Source and evidence record

`SCOPE-R8B.txt` verifies byte-identical ports of the five selected R8
source/test files and unchanged controller/host/platform/linker/build policy.
`r8b-source-record.json` records the base HEAD/branch, changed source hashes,
Felucca changes, logs and all saved candidate artifact sizes/hashes. The BSP
submodule is clean, both source trees remain uncommitted, and no flash,
upload, hardware connection or device enumeration was performed.

## Remaining hardware acceptance

Use [OWNER-BENCH-R8B.md](OWNER-BENCH-R8B.md): BlueZ Secure Connections, Mac and
iPhone pairing (including the encrypted CCCD-write requirement), a saved
bonded reconnect after power loss following disconnect once, MIDI both ways,
30 minutes continuous operation, then at least 12 hours overnight with the
Mac as central. The bench gives exact USB line names, values/transitions,
profile differences, timing/counter criteria and failure-capture instructions.
Native models and successful builds do not establish physical RF continuity,
Apple compatibility, flash timing or audible/audio timing acceptance.
