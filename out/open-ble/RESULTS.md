<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# Open BLE port results — 2026-10-21

Uncommitted in `/root/fm1/fm1-base-open`, branch `open-ble`. No device access,
flashing, commits or pushes. Source: Felucca `dfdd8dc`, frozen r7 spec.

## Size (bytes)

| Template variant | Application flash | Static RAM (.data + .bss) | Pool |
| --- | ---: | ---: | ---: |
| No BLE, measured | 80,172 | 28,320 | 69,384 |
| Open BLE, measured | 189,564 | 34,336 | 96,124 |
| Open minus no BLE | 109,392 | 6,016 | 26,740 |
| JieLi peripheral, README estimate | 207,148 | 74,400 | 93,960 |

The published JieLi row is the measured no-BLE baseline plus README's rounded
+124 KiB flash, +45 KiB RAM, +24 KiB pool; it is **not a fresh closed build**.
Open adds 106.83 KiB flash, 5.88 KiB static RAM and 26.11 KiB pool. Relative to
that JieLi estimate, it uses 17.17 KiB less flash, 39.12 KiB less static RAM and
2.11 KiB more pool. The template's default also enables central, whose separate
published increment is +8 KiB flash/+1 KiB RAM.

Additional open diagnostic allocation: 20,480 B trace in RAMTEXT (outside
static RAM/pool in the table), 2,152 B additional retained noinit log/breadcrumbs.
Open RAM code is 3,112 B; RAMTEXT has 9,176 B spare after trace. Pool has
247,940 B spare (8,192 B required), static RAM 63,968 B spare. DMA is
`0x01C36688..0x01C3777C`, within the verified pool SRAM class.

The final no-BLE sizes equal the initial baseline sizes. Both final builds
pass RAM-only flash-code, ROM-reference, multiply/load, memory and source-link
checks. Open/no-radio register audits omit the unlinked closed adapter.
The open link contains no archives, SDK radio objects, closed shim, WiFi or lwIP.

## Artifact

- `template/build/template-open.fwsc` — 610,136 bytes (fixed package padding;
  application size above), identity `FM-1_1100`.
- SHA256: `b899c915df7c456b92beba0997d229fe5d1ad1ca39c9dcfd443a429cb6a6893b`
- Final build log: `out/open-ble/open-build.log`.
- No-BLE log: `out/open-ble/no-ble-build.log`.
- Mode-specific binary, symbols and link map: `template/build/template-open.*`.
  Generic `template.elf` / `template.bin` also contain the final open build.
- The BSP's two modified files remain uncommitted in the submodule;
  `out/open-ble/felucca-bsp.patch` preserves their diff.

## Validation

- `all-ble-tests.log`: S1/S2/S3/S3.1 and the object-store variant pass under
  ASan/UBSan, with leak checking enabled. F-24/F-28, real legacy/SC SMP and
  encrypted MIDI, connection-update/instant matrices, the 600-second regression,
  resumable work, full-queue/off-on/advertising-pause behavior, actual platform
  MIDI counters, calibration guards, and 10,474 byte-cut cases are covered.
  TinyCrypt aes_encrypt.c retains the original runner's targeted shift-base
  sanitizer exclusion; the other UBSan checks remain enabled.
- `host-build.log` / `host-run.log`: open template simulator builds and runs
  its UI/DSP test, with BLE_OFF. The normal simulator/template test passes too.
- `build-policy-tests.log`: central rejection, template default preservation,
  negative application MMIO and injected archive rejection all pass.
- `update-tests.log`: the actual open .fwsc passes OTA-entry and loader
  simulations under ASan/UBSan. Leak checking alone is disabled for these
  existing process-exit package-buffer fixtures.
- `base-tests.log`: wider base checks pass except mirror/session and its viewer
  fixture. The same 200-row failure reproduces with unchanged HEAD files in
  `baseline-mirror.log`. The base runner's skipped update path was covered
  separately against the open package.
- emcc is unavailable: browser build/run not executed. Its open selection
  uses the same BLE_OFF host backend, without linking the controller.

## Risks / unproved

No physical timing, audio/USB coexistence, RF anchor, stack-margin, calibration,
pairing/reconnect or USB log delivery claim is made for this fm1-base image.
Inherited A9, D-1/D-6, slow-consent and unqualified entropy limits remain. A/B
snapshots remove destructive bond compaction, but a bond changed during a link
is volatile until disconnect/commit, and a failed boot commit can reuse the
prior seed lineage. Every boot spends one sector erase; see the wear model.

A clean-room tooling exception is recorded in docs/OPEN-BLE.md: initial builds
used the existing register checker, which internally scanned excluded
`device/radio.c`. No contents were returned or used for implementation. The
checker now skips that unlinked file for open/no-radio audits. This report
does not claim that no tool ever read the excluded path.

The module/IRQ map, persistence design, licensing/spec-gap notes, exact enable
commands and owner bench checklist are in `docs/OPEN-BLE.md`.
