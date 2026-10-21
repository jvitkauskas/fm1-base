<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# Open BLE-MIDI for fm1-base

This is an uncommitted port of the clean-room WL82/AC79 controller from
`/root/fm1/Felucca-ble`, `ble-controller`, S3.1d commit `dfdd8dc`. Normative input
is the frozen `/root/wl82-ble-spec-snapshots/r7/`. The source controller passed
the owner's device bench through S3.1d. **This fm1-base integration has not been
flashed or tested on hardware.** No devices were accessed, and nothing was
committed or pushed.

## Build and application interface

For a firmware's fm1.toml:

```toml
ble = "open"
ble_central = false
```

Or select the open variant of the unchanged template for one build:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble
```

This writes `template/build/template-open.fwsc`; it overrides `ble` and turns
central off for that invocation. `ble = "open"` with `ble_central = true` is a
clear configuration error. Central support needs a future specification
extension. The template's default stays `ble = true`, `ble_central = true`:
its original JieLi stack selection is unchanged. `--no-ble` builds
`template-usb.fwsc` and excludes both stacks. No closed-stack build was run.

Apps keep `TO_BLE`, `PORT_BLE` and the peripheral plat.h API. They supply
`OBJ_BLE` and a nonoverlapping 8 KiB A/B pair in `APP_STORE_MAP`, as before.
The template already uses `0xDE000`/`0xDF000`; no new app object is required.
The open format is incompatible with the closed stack's OBJ_BLE contents;
switching stacks does not migrate pairing, and saving in either format
replaces the other format.

The profile is one peripheral BLE-MIDI link, ATT MTU up to 200, legacy and
Secure Connections Just Works pairing, two peer bonds, peer-RPA resolution,
encrypted reconnects, and central-initiated map/connection updates. There is
no central/scanner role or WiFi. Advertising name: **FM-1 Open BLE**.

`BLE_OFF`, `BLE_STARTING`, `BLE_READY` and `BLE_FAILED` describe lifecycle.
READY means the host has synchronized; `plat_ble_linked()` means a connection
exists and `plat_ble_connected()` additionally requires MIDI notification
subscription. `plat_ble_enable(0)` is applied in main, stops advertising and
requests link termination, with retry while a link remains. On resumes
advertising without reinitializing NimBLE. Calls from app/tick contexts only
publish the desired state. Main and LCD yield points service host work with
a reentrancy guard; no host callbacks or allocation run in the executor.

At boot, the seed is advanced before audio/USB regardless of APP_BLE_AT_BOOT.
After platform setup, APP_BLE_AT_BOOT controls the normal start. A retained
active-run marker plus a new fault or watchdog reset inhibits automatic
Bluetooth restart; manual `plat_ble_start()` still starts it. Clean updater /
UBOOT exits clear that marker. A cold power-on is not treated as a crash.
The 48 x 43-character log ring and breadcrumbs survive warm resets in
`.noinit`; power-on contents are validated by magic, not assumed preserved.
A start resets the ring's line count. Fatal reporting does not re-enter the
host.

BLE input/output use the existing four-byte USB-MIDI event representation,
including SysEx and real-time messages. TX/RX rings each hold 128 packets.
The host retains queued notifications on temporary allocation/backpressure.
The platform's actual MIDI functions update PORT_BLE counts and activity,
excluding real-time messages. A subscribed full BLE queue contributes **zero**
to the minimum returned by `plat_midi_out_room(TO_USB | TO_BLE)`; an absent
listener is ignored. Producers must honor that result. MIDI is carried into
the template synth and screen; the template's keys transmit to USB and BLE.

## Module map

| Module | Responsibility |
| --- | --- |
| `open_ble/ble_logic.c`, `ble_controller.c` | Calibration record decoding, in-memory HCI, command queues, redacted trace |
| `ble_link*.c`, `ble_security.inc`, `ble_privacy.c`, `ble_timing.c` | Peripheral LL, encryption, instants, resolving list, resumable work/budgets |
| `ble_crypto.c`, `ble_seed.c` | AES/CCM glue, hash generator, boot seed/readiness |
| `ble_bonds.c`, `ble_store.c` | Canonical identity-keyed cache, net-state comparison, atomic object snapshot |
| `ble_host.c`, `ble_midi.c`, `ble_npl.c`, `ble_heap.c`, `port/` | NimBLE application/NPL, MIDI codec, bounded heap/config/libc declarations |
| `open_ble/hal/fm1_ble*.h` | Radio/baseband, DMA, TIMER0, nesting-safe locks, AES and read-only calibration access |
| `device/ble_open*.c` | fm1-base seam, executor/host hooks, lifecycle, log output |
| `bsp/chip/fm1_isr.S`, `fm1_flash.h` | Optional ISR accounting, safe flash completion, idle/calibration write guards, plain reads |
| `device/unity.c`, `main_fm1.c`, `plat_fm1.c`, `app.ld` | Boot phases, poll/yield, storage/MIDI integration, explicit memory sections |
| `third_party/nimble/`, `tools/nimble_build.py` | Pinned Apache NimBLE 1.9.0 and bundled TinyCrypt source list |

`open_ble/hal/` is a narrowly scoped BSP extension for the undocumented radio
peripherals and dedicated executor. The register checker explicitly documents
this boundary, checks all other open-stack software recursively, and retains
the checks on device/, include/, lib/ and app code. A negative fixture verifies
that application register access is still rejected. The unlinked closed
`device/radio.c` is omitted from open/no-radio audits; builds that compile it
retain the original audit.

The BSP submodule has **uncommitted changes** to the two files above. Preserve
them with the parent changes. `out/open-ble/felucca-bsp.patch` captures them
against BSP commit `33cac693903e753e8783baeed05c19cadfb04812`; the parent
submodule commit pointer has not been changed.

## Platform and interrupts (r7 04 §§0.6, 0.8)

Higher numeric levels preempt lower ones; the template uses CPU 0 only.

| Level | Line | Handler / work |
| --- | --- | --- |
| 7 | 1 | Fatal CPU exception; does not return |
| 6 | — | Unused |
| 5 | 29, 45 | BLE RX and event-close; above every returning long handler |
| 4 | 63 / TIMER5 | 10 kHz panel scan, 2 kHz USB/TRS, app's optional 1 kHz tick |
| 3 | 11 / ALNK0 | Audio render and USB audio mixing/tap |
| 2 | 4 / TIMER0 | LL executor, below audio and scan/USB |
| Main | — | NimBLE, SMP/ECC, object commits, app UI |

P2 runs after JEDEC/unencrypted-window setup and before LCD/audio/USB. It reads
calibration, loads OBJ_BLE and commits the next seed. P3 attaches all three
controller vectors masked, after audio setup and before the vector write lock.
P4 performs radio bring-up only when Bluetooth is requested. With the BSP
interrupt gate enabled, the TIMER0 self-test requests exactly one entry within
1 ms before NimBLE can open an advertising instance. A failed probe/self-test
fails startup. Baseband lines unmask only when an instance opens.

TIMER4 supplies continuous 24 MHz ticks; the BSP's TIMER5-maintained fm1_ms is
the monotonic host clock. TIMER0 uses a 1 us requested expiry and 1 ms autonomous
backstop; it is never shared with an app. Pending requests coalesce and a
request during a pass causes a subsequent entry. The nesting-safe ICFG save /
restore preserves the interrupted state, including in flash and app locks.

Every returning handler above the executor is wrapped with nested preemption
accounting. The executor subtracts that time once. Limits retain r7 values:
dispatch <=7,500 us without negative samples, own work stops before the next
item after 250 us, completed own pass <=250 us + longest item, item <=100 us,
refill <=80 us. Per-kind counts/maxima/overruns and max opcode are exposed.
The self-test proves dispatch wiring only; none of the physical timing bounds
is established for this port by a native test.

DMA is explicitly NOLOAD inside the pool's verified SRAM class, with linker
assertions and target build checks. The 20,480-byte trace is separately NOLOAD
in RAMTEXT; startup clears it. The host ring is inside cleared BSS. NimBLE
static pools and software buffers are counted in the pool; stack ranges and
hardware guards retain the BSP layout. The radio's startup silences audio via
radio_quiet; the executor stays below audio afterward. The USB log includes
audio late/xrun/wall maxima. New stack margin, actual IRQ/flash blackout and
coexistence timing remain owner bench measurements.

The only TinyCrypt source change calls a pure out-of-line 32x32->64 multiply
on this target, following `bsp/docs/CPU.md`. Original notices are intact; the
final target disassembly reports no multiply inlined beside a load.

## Persistence and calibration

The original fixed seed sectors `0x94000`/`0x95000` and bond sector `0x96000`
are **not written by the open stack**. They remain application XDATA0 space.
OBJ_BLE is an fm1-base A/B object with an explicit 1,108-byte payload:

| Payload offset | Contents |
| --- | --- |
| 0..15 | `FMOB`, version 1, zero reserved fields |
| 16..79 | Unchanged r7 64-byte `WLSD` seed record, including CRC/flags/credit/sequence |
| 80..1103 | Eight canonical 128-byte `FLBD` records; absent slots are FF |
| 1104..1107 | CRC-32 of the preceding payload |

The existing store erases the inactive sector, writes the payload from +256,
then writes its CRC-protected 32-byte header last and verifies the result.
Load validates both headers/payloads and falls back to the previous valid
copy. Its final 256 bytes remain blank, avoiding the loader update-marker
shape. No raw NimBLE structs or private radio addresses become durable keys.
Public and random-static identities (including normalized identity types)
key OUR_SEC / PEER_SEC / CCCD records. Orphan CCCDs are discarded at snapshot;
delete/recreate and subscribe churn that return to the durable net state do
not write. Failed commits do not advance the durable cache baseline.

Boot commits the next seed together with unchanged bonds before radio start.
After a link ends, pending bond net changes commit together with the unchanged
seed before advertising resumes when storage is idle. Whole snapshots replace
the old single-sector journal, eliminating its destructive compaction window.
An app save may synchronously pause an idle advertisement and resume it after
the save. A live or newly accepted link makes the save return -8: callers
must retry after disconnect. The RAM driver rechecks idle with IRQ exclusion
before each erase/program, so acceptance cannot race an issued write. Plain
XIP object/calibration reads never switch flash off during a connection.

The guard preserves the RAM-only completion contract: pre-issue failures are
NOT_STORED; a late-ready/readback error is UNVERIFIED. Once issued, flash is
polled from RAM until ready; beyond the extended bound it resets without
returning to XIP while busy. The reset/ROM behavior of a still-busy part is
unproved, as in r7. Entropy classes FAILED/FIRST_BOOT/CHAINED/PROVISIONED/
UNPERSISTED and provenance/credit fields are retained. Unqualified clock/R64/
RF diversifiers receive zero entropy credit. No new CSPRNG qualification is
claimed. HCI Reset, link rekeys and host reset do not write the boot seed.

**Wear:** one sector erase per boot, alternating the two sectors, plus one
per durable bond net change. This deliberately spends more wear than r7's
60-record seed log. At a hypothetical 100,000 erase cycles per sector, the
pair allows roughly 200,000 commits: about 548 years at one boot/day, 139 days
at one boot/minute, before bond writes. This is arithmetic, not qualification
of this unit's flash endurance; rapid reboot loops consume it quickly.

**Crash windows:** before the new header commits, the old whole seed/bond
snapshot wins; after it commits, the new whole snapshot wins. No period
requires an erased-only bond sector. A cut before the boot seed commit can
reuse the prior seed lineage at the next boot; physical diversifiers remain
uncredited. Pairing while linked is RAM-only until disconnect/commit, so a
power cut before that commit loses the newly changed bond. A restore of an
older OBJ_BLE can roll back the seed chain; this format is device-local
persistence, not rollback-proof secure storage. Sequence wrap of the seed is
covered; the base object's 32-bit save sequence has the existing store's
long-term wrap limit.

Calibration is read exactly from first VM `0x93000..0x93FFF` and BTIF
`0xE9000..0xE9FFF`, as r7 04 §7 specifies, without hunting app data for record
shapes. The former overlaps XDATA0 and the latter overlaps XDATA1. Both whole
sectors are therefore **read-only in open builds**, including through the raw
data API. The store map is checked for aligned, disjoint A/B pairs within
allowed store windows. Other XDATA sectors are untouched by this controller.
A calibration sector already overwritten by an app is not reconstructed:
defaults/self-calibration apply; without a valid factory-derived public
identity this port fails startup instead of inventing an address. The unit's
calibration record is never written.

## Spec-gap notes

1. r7's Felucca flash allocation and seed-log wear design become one fm1-base
   A/B snapshot object. Record semantics/readiness/boot-before-radio ordering
   stay; physical slot rotation and erase frequency change as above. This is
   an integration adaptation, not an edit to the frozen specification.
2. The base BSP raises its supply rails before startup; the radio still reads
   and reports the clock/P33 state and does not change the system PLL. The new
   supply/startup/audio environment needs fresh device timing qualification.
3. IRQ priorities use fm1-base's actual audio 3 and scan/USB 4; baseband is 5,
   TIMER0 executor 2. Shared nested accounting replaces Felucca-specific audio
   flags. Physical budgets and 25% nested-stack margin remain unproved here.
4. RAMTEXT is 32 KiB in this BSP. Trace, host ring and DMA have explicit
   sections and are counted/cleared; no orphan section placement is relied on.
5. CDC console status is mapped to the existing USB log protocol. The full
   original controller/host/security/privacy/timing counters stream in rows
   of at most 187 ASCII bytes, with continuation for longer rows. The remote
   tool allows eight seconds for a dump. No CDC function is required.
6. The host simulator/browser build has FM1B_BLE=0 for this variant; it links
   no controller and reports BLE_OFF. Native radio simulations separately
   exercise the actual host/controller. Default JieLi simulator behavior is
   retained.

## License, provenance and clean-room tooling

Owner-authored stack/port/test files carry **MIT OR MPL-2.0**, Copyright (c)
2026 Julius Vitkauskas. Existing fm1-base and BSP files remain MPL-2.0 with
original notices. NimBLE remains Apache-2.0; bundled TinyCrypt keeps BSD notices.
See `LICENSING.md`, `CREDITS.md`, `LICENSES/MIT-BLE.txt`, and the vendor pin /
change note. The radio tables have the owner's Apache-2.0 attribution verbatim
in their header. **The open application links nothing of JieLi's except those
transcribed tables.** Runtime factory calibration reads are not linked data.
The package still carries the existing three SDK package-data files; open
builds never invoke the closed SDK radio source/library-copy path.

No excluded source contents were returned to the implementer or used to
implement the port. There was a tooling exception: initial firmware builds
ran the preexisting register checker, which internally reads every device C
file, including excluded `device/radio.c`. Once identified, open/no-radio
audits were changed to skip that unlinked file. This exception is recorded
rather than claiming that no tool ever read an excluded path. The excluded
ble/ and examples/wifi/ contents and the other prohibited trees were not
consulted for implementation. There was no flashing, device access, commit
or push.

## Verification, artifacts and limits

Run from this repository:

```sh
python3 tests/run_open_ble_tests.py
python3 tests/open_ble_build_test.py   # after the open firmware build
python3 tools/build.py template --open-ble --host
template/build/host/template-open_host template/tests/open.txt template/build/host
```

S1 (including measured-power variant), S2, S3, S3.1 and the object-store variant
passed under ASan/UBSan. This includes actual NimBLE legacy/SC SMP, encrypted
MIDI/reconnect, F-24 Reset/RPA restoration, F-28 repeated pairing with retained
and changed IRKs/new RPAs, update/instant matrices, missed-close and late
instant handling, and a 600-second run crossing the 89.478-second stale-first
phase regression and multiple tick wraps. Resumable Reset/CCM/SHA timing tests
are cost models, not silicon timing evidence.

New tests cover the actual platform MIDI functions (full mixed-port queues,
counts/activity/real-time exclusion), lifecycle/crash/log seam, object-store
readiness/failure/retry and 10,474 byte-cut seed/bond cases. The final source
link map excludes all archives and closed SDK/shim paths; a negative test
injects an archive and requires rejection. An application MMIO negative test
and central-selection error test pass. Both native host variants run; the
open one stays BLE_OFF. Emscripten/emcc was unavailable, so the browser build
was not executed; it uses the same radio-free host source/selection.

`out/open-ble/all-ble-tests.log`, `host-build.log`, `host-run.log`,
`build-policy-tests.log`, `no-ble-build.log` and `open-build.log` hold results.
The real open package also passes OTA-entry and update-loader simulations
under ASan/UBSan (`update-tests.log`); leak detection is disabled only for
these preexisting fixtures, which retain their package malloc until exit.
The wider base runner passes encoder/TRS/storage/fastmath/installer/backup/
remote/template checks but fails the mirror session and downstream viewer
fixtures. The identical 200-row mirror failure reproduces using unchanged
HEAD sources (`baseline-mirror.log`); no mirror changes were made.

Build-size results and SHA256 are in `out/open-ble/RESULTS.md` and
`manifest.json`. The application ELF/map/symbol files are under
`template/build/`; the last firmware build is the open variant. The .fwsc
package size includes fixed padding and is not the linked flash size.

Known inherited limits include A9 disconnect provenance still open; Android
choosing legacy by its own SMP negotiation (BlueZ SC also supported); D-1
MIC-tail handling; peer-only privacy/D-6 (no local RPA); unknown RF anchor latch
behavior/A1/A7/B22; slow pairing consent; unqualified entropy and RF range /
temperature. The journal crash window is replaced here, with the pre-commit
bond loss and boot-seed rollback windows stated above. Hardware still has to
prove interrupt/CCM/flash budgets, audio/USB coexistence, stack margin, supply
behavior, reconnect/update behavior and USB log delivery on this integration.

## Owner bench checklist (not executed)

1. **Boot/advertise:** install the identified open package using the owner's
   normal process. Save `python3 tools/fm1_remote.py log`. Require READY,
   executor_selftest=1, valid public identity/calibration, stored boot seed,
   DMA canaries intact, and FM-1 Open BLE visible with the BLE-MIDI UUID.
   Capture before pairing so later timing maxima have a baseline.
2. **Android legacy:** pair through a BLE-MIDI app, subscribe, confirm
   linked=1 and connected=1, encrypted/key_size=16, pair method 1, and MIDI
   both ways. Android's legacy choice alone is not a failure. Exercise delayed
   consent and retain a phone HCI snoop if pairing stalls or A9 disconnects.
3. **BlueZ SC:** pair and subscribe with an SC-capable central. Require method
   2, encryption and MIDI both ways. Record actual SMP/air evidence if it
   selects otherwise; do not change the LL/HCI version to force SC.
4. **Template MIDI:** send phone/BlueZ note-on/off and SysEx/real-time traffic
   into the synth and check PORT_BLE counts/activity. Use
   `python3 tools/fm1_remote.py tapkey 7 wait 200 screen log` for a template key
   transmitted to the subscribed central and USB. Stress backpressure without
   losing owed note-offs; retain drop counters and audio late/xrun counters.
5. **Durable reconnect:** disconnect, wait for bond_pending=0 and a successful
   bond write, then power-cycle. Require a new seed sequence, retained bond,
   no pairing prompt, encrypted MIDI both ways and no negative LTK reply.
   Repeat Android and BlueZ, fresh RPAs, HCI reset and two-peer replacement.
   Re-pair after forgetting only on the phone to repeat F-28.
6. **Off/on:** `python3 tools/fm1_remote.py press REC wait 1100 release REC
   wait 400 screen log` toggles the template through plat_ble_enable. Require
   link termination and no advertisements while off; repeat while already
   idle. Toggle on and require advertisements/reconnect without a crash.
   SAVE persists the template's changed setting; reboot with saved off and
   on settings to check APP_BLE_AT_BOOT. Verify SAVE while advertising pauses
   and resumes it; SAVE while linked must refuse cleanly. A prior crash
   should inhibit automatic start until an explicit on/start.
7. **Connection update:** use the central's supported interval controls to
   request 30, 15 and 7.5 ms, offsets 0/mid/max and legal maximum windows,
   in both interval directions with MIDI/audio/USB load. Retain update rows
   showing receipt/program/instant/outcome and event-counter rate. Run more
   than 120 seconds after each change to cross 89.478 seconds; keep epoch
   stable and verify no frozen event counters, late instants or supervision
   loss. A common-clock LL sniffer is still needed to prove RF anchor timing.
8. **Timing/logs:** dump `python3 tools/fm1_remote.py log` at boot, after
   pairing, after each update and after ten-minute encrypted 7.5/15 ms load
   runs. Require zero negative dispatches, dispatch <=7,500 us, item <=100 us,
   own pass <=250 us + longest item, no pass-budget overruns, refill <=80 us,
   intact canaries and adequate audio/stack margin. Keep idle flash erase gaps
   separate from live-link scheduling. Confirm complete USB log rows and the
   terminating frame; retain package hash and logs with all bench records.
