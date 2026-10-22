<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# Owner bench — Open BLE R2

Firmware: `template/build/template-open.fwsc`

SHA256: `2fbf32deec62b261c93629c88668c30a95c337532f5d22db0952539a51a66e29`

Generated from `docs/OPEN-BLE.md`; record each result and attach the referenced logs.

## Owner bench checklist (R2, not executed by the implementer)

Keep the firmware SHA256, phone/Mac/Linux model and OS version, app version,
UTC times, USB logs, `btmon`/PacketLogger captures and the result of each item.
Use a new output directory per session; keep an initial `ble` and full `log`.

1. **Boot and basic MIDI:** owner installs the identified diagnostics-on
   package. Require READY, selftest 1, no controller error/canary failure,
   stable public identity and MIDI UUID. Pair legacy Android and SC BlueZ,
   record method/key size/bond/encryption/subscription/MTU and MIDI both ways.
2. **Gap check, five cycles:** establish an encrypted, subscribed link **before
   each** off/on cycle (REC held about 1.1 s in the template). Capture `ble`
   before off, while off, after on and after reconnect. Verify advertising
   events freeze while off; link terminates cleanly; the next session uses
   the boot identity, ADV_IND/data/parameters and accepts its first connect.
   Retain all `worker_gap` / `worker_gap_max` rows and session seen/accepted
   counts. Any >7.5-ms live/unknown gap is a failure to investigate; record
   idle flash gaps separately, with epoch and state. Repeat five times.
3. **F-28, phone:** retain the FM-1 bond, forget only on the phone and re-pair.
   Repeat for retained identity/IRK and, where controllable, changed IRK/new
   RPA. Require replacement without negative LTK loops, an encrypted 16-byte
   link, MIDI both ways and durable reconnect after disconnect/power cycle.
   Capture SMP/identity-distribution evidence; a UI pairing name is not enough.
4. **Connection updates:** run the desktop bench at 7.5 / 11.25 / 15 / 30 ms,
   both directions, then reconnect. Where central controls allow, cover
   WinOffset zero/mid/max and legal maximum windows. Hold each interval >120 s
   under MIDI/audio/USB load; keep receipt/program/instant/outcome, stable
   epoch, event-counter rate and wall time. Require no late instant,
   supervision loss, MIC failure or frozen event counter.
5. **Ten-minute load:** encrypted, subscribed MIDI both ways at 7.5 and 15 ms
   as supported, with synth/UI/USB audio active. Keep start/end and 60-s logs,
   note loss/stuck notes, drop/MIC/reset counts, gap records, timing maxima,
   canaries, audio late/xruns and stack margin. Then run the overnight tool.
6. **SAVE / boot / crash inhibit:** SAVE while advertising must pause/resume
   cleanly and attribute flash gaps; SAVE while linked must refuse with -8
   without disrupting MIDI. Save off/on settings and reboot to exercise
   APP_BLE_AT_BOOT both ways. Using the owner's established crash fixture,
   verify a warm fault/watchdog inhibits automatic radio restart and explicit
   on resumes it; cold power-on and clean exits must not be crash-inhibited.
7. **Two-peer replacement:** bond A and B, replace the oldest with C, then
   reconnect B/C (including fresh RPAs) with no prompt and encrypted MIDI.
   Confirm A needs re-pairing and no stale CCCD/IRK/key survives replacement.
   Check one atomic bond snapshot completes after disconnect and survives
   power loss; record bond_pending/writes/failures and peer identities safely.
8. **Apple — iPhone:** record iPhone/iOS and BLE-MIDI app versions. Connect
   through an iOS BLE-MIDI app or GarageBand's Bluetooth MIDI controls. Verify
   discovery, empty MIDI read after pairing, Just Works/bonding, encrypted MIDI
   both ways, MTU and subscription. Record the peripheral's first request
   (15/15 ms, latency 0, timeout 6 s, no earlier than 5 s) and the actual
   central-selected interval; distinguish accepted 30 ms from the requested
   value. Exercise screen lock/background, out-of-range recovery, five off/on
   cycles, reboot/RPA reconnect without prompts, and forget/re-pair F-28.
9. **Apple — Mac:** record Mac/macOS version. Open **Audio MIDI Setup → MIDI
   Studio → Bluetooth**, connect FM-1 Open BLE, and use Test MIDI Setup and a
   MIDI app/DAW to verify both directions. Repeat bonding/reconnect, intervals,
   MTU, subscription, sleep/wake and device off/on; retain PacketLogger or
   other available Bluetooth traces plus USB counters and audio observations.
10. **Apple — GATT cache upgrade, both:** retain the R1 bond, upgrade to the
    identified R2 firmware using the owner's normal process, reconnect without
    forgetting, and capture restored Service Changed CCCD plus a full-range
    indication and confirmation. Require rediscovery including the new DIS,
    correct GAP name, no stale MIDI handles and working encrypted MIDI.
    Disconnect before confirmation and repeat reconnect/power-cycle; the
    indication must recur. If the old client never subscribed, record that
    fact, forget/re-pair once, then repeat with subscription enabled. Record
    `gatt_invalidations`, discovery handles, CCCD and confirmation evidence.
