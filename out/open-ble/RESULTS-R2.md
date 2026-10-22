<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# Open BLE round 2 — 2026-10-21

Implemented on fm1-base `open-ble`, HEAD `f08c9c2` (port `3b1e677`),
BSP `62a01ca7affe8de73707e309b57e98f1734c8b2d`. All R2 work is uncommitted.
No device enumeration/access, flashing, commits or pushes. BSP is unchanged.
The existing BSP adaptations remain optional behind `FM1B_BLE_OPEN`.

## Deliverable

- Diagnostics **on**: `template/build/template-open.fwsc`, 610,136 bytes,
  identity `FM-1_1100`.
- SHA256: `2fbf32deec62b261c93629c88668c30a95c337532f5d22db0952539a51a66e29`
- Build log: `out/open-ble/r2-open-build.log`.
- Documentation/rules/citations: `docs/OPEN-BLE.md`, especially **Apple**.
- Owner checklist: `out/open-ble/OWNER-BENCH-R2.md` (also in the document).
- Bench: `tools/ble_conn_update_bench.py`; soak: `tools/ble_overnight_soak.py`.
- Exact USB port selection and compact counters: `tools/fm1_remote.py --port NAME ble`.
- Machine-readable sizes/hashes: `out/open-ble/r2-manifest.json`.

Build command (the required environment):

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble
```

## Findings and changes

**R1 timer gap:** its 17,804-us maximum cannot be attributed retrospectively.
The runtime now records the maximum separately and the last four >7,500-us
inter-IRQ gaps, with end timestamp, states at the interval endpoints, overlapping
known operations, and epoch at the returning IRQ. Link at either endpoint is
conservatively labeled `link_up`; a gap spanning disconnect can therefore have
`state link_up`, `end_state starting` and `reason flash_write`. That does not
mean flash was issued with a link: the idle guard is checked before each
physical operation. Unknown causes remain explicitly unknown. The continuous
TIMER4 clock survives wrap/missed interrupt ticks; history survives HCI Reset.
The scopes nest correctly, including object-save and individual flash writes.

Calibration occurs once before TIMER0 starts. `plat_ble_enable(1)` after off
reuses that radio/NimBLE instance, rather than entering calibration again.
Platform and HAL guards reject calibration with an active instance/link.
Advertising preparation is sliced (256-byte clearing / 64-byte frame writes);
stop polling is nonblocking and quarantines after its 20–100-ms wall-time bound.
There is no designed long timer blackout on re-enable. Idle flash can legitimately
block interrupts beyond 7.5 ms while XIP is unavailable; R2 labels that activity.
No new physical blackout duration or scheduling bound is claimed.

**R1 first BlueZ reconnect:** no software restart defect explaining it was found.
Boot, re-enable, disconnect and save-resume share the same configuration; the
only intended change from R1 is the Apple-friendly 20-ms advertising interval.
RX acceptance state and both TX descriptors are ready before the hardware
publication. A new test injects CONNECT_IND on the first IRQ unlock, before
start returns, over six sessions. It succeeds in every session and separately
counts a malformed request. New per-session seen/accepted counts plus four
session records support the next owner bench. Seen is decoder/RX evidence,
not proof that every transmitted RF packet reached this peripheral. Observation
B may be BlueZ-side; that remains unproved. On while termination is pending
also waits until the old link is gone before advertising.

**Apple/MIDI:** fixed 15-ms / latency-0 / 6-s L2CAP request at >=5 s, after SMP;
20-ms connectable advertising; local MTU 517 / maximum attribute 512; GAP name
`FM-1 Open BLE` and volatile writable name; unspecified appearance 0; encrypted
MIDI values and no unsolicited pairing request; retained SC/legacy Just Works
bonding. DIS is appended without shifting existing handles. A full-range
Service Changed indication is sent on each encrypted connection once its CCCD
is enabled/restored, with confirmation and durable CCCD behavior in NimBLE.
The RX ring grows to 512 USB-MIDI packets to hold one worst-case expanded
maximum value; TX stays 128. Release firmware revision follows `--release`;
the development revision is `open-r2`.

Each rule and source is documented in the Apple section. The current Apple
ADG (2026-09-21, chapter 58) calls for a different timeout range from archived
QA1931; the six-second choice follows the current guide. The TX Power Level
AD recommendation remains open because calibrated dBm is unknown; no value
was fabricated. No Apple hardware/certification result is claimed.

**Tooling:** compact `LG 01` readout and explicit USB port; missing `end` is an
error. The bench covers 15→7.5→11.25→15→30→15 ms, counter rates, epoch stability
and reconnect through USB counters. The owner-only BlueZ soak requires a bonded,
encrypted, subscribed link, plays `aplaymidi` notes every 5 s, cycles
15/30/11.25/15 ms every 10 min, samples every minute, reconnects/counts drops,
and writes raw USB, JSONL, btsnoop and an end summary. It has not been run on
the owner's desktop. No optional debug HCI Reset USB command was added.

## Footprint — bytes

| Template | App flash | Static .data+.bss | Pool | RAMTEXT code | RAMTEXT trace |
| --- | ---: | ---: | ---: | ---: | ---: |
| Open, diagnostics on | 192,880 | 34,564 | 99,212 | 3,112 | 20,480 |
| Open, diagnostics off | 184,368 | 33,756 | 99,212 | 3,112 | 0 |
| Saved by diagnostics off | 8,512 | 808 | 0 | 0 | 20,480 |
| No BLE (unchanged from R1) | 80,172 | 28,320 | 69,384 | 3,000 | 0 |

RAMTEXT total is 23,592 on / 3,112 off (9,176 / 29,656 bytes spare).
Static RAM excludes separately allocated pool, RAMTEXT, retained noinit and
stacks. Pool headroom is 244,852 bytes; its R1 increase of 3,088 bytes is mainly
the larger RX ring, plus the GAP name buffer. The .fwsc size includes fixed
padding and is not application flash usage. No closed/vendor build was run.

Use TOML `ble_diag = false` or CLI `--ble-diag off`; default stays on. This
compiles out the 20-KB trace, full dump/frozen/register snapshots, per-kind timing
and name tables, per-reason/update record tables and detailed HCI/ACL counters.
Release keeps the actual scheduling budget/preemption accounting and minimal
health/timing/error/MIDI/parameter counters and gap ring. The off build is
archived in `out/open-ble/r2-diag-off/`; its package SHA256 is
`5570219ee60be1eb0c9d935943c200beda84e563cd3561357dd46afccdb30994`. Both target builds enforce their expected trace size.

## Verification

- **PASS** `r2-all-ble-tests.log`: all S1 (also measured TX variant), S2, S3,
  S3.1, object-store and diagnostics-off production integration variants under
  ASan/UBSan, with leak checking. The inherited TinyCrypt AES shift-base
  exception remains; no new sanitizer exemption was added.
- **PASS** new gap tests in both modes: strict threshold, tick wrap, millisecond
  timestamp, state/reason/epoch, nested operation scopes, four-record retention,
  independent maximum and actual platform counter-reset preservation.
- **PASS** new HAL tests in both modes: no published instance during preparation,
  first-event ADV_IND and CONNECT_IND acceptance, consistent address/data/params,
  six sessions and malformed seen-versus-accepted accounting. Five off/on cycles
  begin with a linked seam and never re-run calibration.
- **PASS** real NimBLE profile tests: public GAP/DIS discovery, writable name,
  appearance, protected MIDI read, no unsolicited SMP request, SMP legacy/SC,
  517-byte MTU and a maximum MIDI value (509 USB packets) without overflow,
  Service Changed indication/confirmation and restored CCCD on reconnect;
  L2CAP request values and >=5-second timing. Existing update/instant/CCM,
  F-24/F-28/privacy, 600-s scheduling and 10,474 persistence-cut cases pass.
- **PASS** offline USB/soak fixtures: termination detection, parser, counter wrap,
  command status, 10-minute interval cycle, notes, drop/recovery and failure
  classification. Tests never enumerate USB/Bluetooth ports.
- **PASS** `r2-open-build.log`, `r2-diag-off-build.log`,
  `r2-no-ble-build.log`: flash/RAM/DMA/trace assertions, RAM-only flash code,
  no multiply beside a load, source-only linking and register boundary.
- **PASS** `r2-build-policy-tests.log`: peripheral selection, diagnostics/version
  flags, negative archive injection and negative application MMIO fixture.
- **PASS** `r2-jieli-free-host-build.log`, `r2-jieli-free-host-run.log`:
  SDK/toolchain environment unset, ordinary host compiler, functional template
  simulation with BLE_OFF. Emscripten/emcc is unavailable; browser not run.
- **PASS** `r2-update-tests.log`: open package OTA-entry and loader simulations
  with ASan/UBSan. Only these preexisting package fixtures use `detect_leaks=0`
  because they retain allocated package buffers until exit.
- **NOT ALL PASS** wider `r2-base-tests.log` with ASan/UBSan: encoder/TRS/storage,
  installer, backup/tool/page and host build pass. Unchanged
  `include/fastmath.h:58` shifts a negative value (fastmath/template cases),
  and unchanged `device/mirror.c:179` indexes past its 988-byte buffer. The
  downstream mirror viewer fails its existing session fixture. The sanitized
  open simulator hits the same fastmath issue (`r2-host-sanitized-run.log`);
  its normal functional run passes. These files/tests have no R2 changes;
  R1 already records the wider mirror failure. No unrelated fixes were made.

## Clean-room boundary and remaining owner work

Implementation content came only from the permitted trees/public specifications
and pinned Apache NimBLE. One initial **filename-only** AGENTS.md discovery
was too broad and traversed `/root`; it returned two unrelated instruction-file
paths, neither opened. No excluded source content was returned or used. All
subsequent source reads were scoped to the allowlist. The owner explicitly
authorized build-tool hashing/packaging of exactly `uboot.boot`, `cfg_tool.bin`
and `cfg/eq_cfg_hw.bin`; their contents were not exposed or consulted for
implementation. Public document versions/hashes are in
`out/open-ble/references/source-record.json` (downloaded PDFs not retained).

The owner's R1 hardware pass remains evidence only for R1. R2 still needs the
five-cycle gap check, first reconnect capture, F-28 phone tests, interval matrix,
ten-minute load, SAVE/APP_BLE_AT_BOOT/crash-inhibit, two-peer replacement, iPhone
and Mac MIDI/bonding/MTU/interval tests, and cached-GATT firmware upgrade tests.
The complete checklist states what logs and radio evidence to retain. New
physical timing, stack margin, audio coexistence, RF/anchor behavior and the
Linux overnight recovery remain unproved until that bench is run.
