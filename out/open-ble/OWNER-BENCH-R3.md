<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# R3 owner bench — Apple first pairing and bonded cache repair

This is an owner checklist, not an implementer device result. Identify the
R3 diagnostics-on `template/build/template-open.fwsc` by the SHA256 in
`RESULTS-R3.md`. The overnight R2 soak remains a separate result.

Record central model/OS/app, firmware SHA256, UTC times, before/after USB logs,
MIDI directions, pairing prompts and final connection status for every row.
Keep btmon/PacketLogger capture where available. Do retained-bond upgrades
before forgetting that peer or pairing additional devices: FM-1 stores only
two identities. An evicted FM-1 bond is a new pairing, not evidence of a failed
retained-bond migration. For all four historical centrals, identify which
corresponding FM-1 bond is actually retained (or which original store snapshot
is under test); do not silently re-pair to pass the upgrade case.

Use the exact USB-MIDI port, with other USB log clients closed. On the owner's
machine, save a compact dump immediately after each connection/disconnect:

```sh
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" ble > "$SESSION-ble.txt"
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" log > "$SESSION-log.txt"
```

The last 32 `ble_event` rows are oldest first, with boot ms and connection
session IDs. Capture early enough to retain the setup. Relevant fields are
listed in `docs/OPEN-BLE.md`; CCCD 9 is Service Changed, CCCD 17 is MIDI.
`sc_sent a 1 b 65535` is the full range; `sc_confirmed a 3` acknowledges
this generation. `disconnect a 19` means central 0x13. Treat counter deltas
per session, not lifetime totals, as acceptance evidence.

1. **Existing Mac bond, firmware upgrade:** retain the current central/FM-1
   keys, install the R3 image by the owner's normal process, then connect
   through Audio MIDI Setup. Expect encryption without a pairing prompt, one
   `sc_sent` followed by one `sc_confirmed`, full rediscovery including MIDI
   and DIS, correct GAP name, and CoreMIDI both ways. Capture `ble` and `log`.
   Disconnect normally, allow `bond_pending` to clear, reconnect, and expect
   zero additional indications with MIDI working. Repeat after a power cycle
   following that completed idle save.
2. **Existing iPhone bond:** the same upgrade/reconnect acceptance, through
   GarageBand's Bluetooth MIDI controls. Expect one indication/confirmation
   for the retained stale bond and none on the next reconnect. MIDI must work
   without Forget/re-pair. Save the event readout after each connection.
3. **Existing Android bond:** encrypted RPA reconnect with no prompt, one
   indication/confirmation, MIDI both ways, and no further indication after
   the disconnect/save/reconnect. Record negotiated MTU and actual interval;
   retain the L2CAP request/result and applied-update events.
4. **Existing BlueZ bond and cache:** retain the old bond and current cached
   service database, including the missing-MIDI case if it is still available.
   Connect/encrypt without remove/pair. Require one full-range indication and
   confirmation, discovery of GAP/GATT/MIDI/DIS, `FM-1 Open BLE` as the readable
   Device Name, an ALSA BLE-MIDI port, and MIDI both ways. Save the central
   discovery trace plus USB event log. Reconnect after an idle save; require
   zero additional indications. If there is no `cccd_restore a 9 b 2`, record
   the missing/disabled subscription rather than calling a fresh pairing a
   successful repair. A trace is needed if a subscribed central still omits
   MIDI after the full-range indication.
5. **Mac first connection after Forget:** after the retained Mac test, Forget
   only through the Mac's normal UI and connect once through Audio MIDI Setup.
   Accept pairing. Require the UI to stay connected on that same first attempt,
   with encrypted MIDI both ways and no 0x13 disconnect after consent. The
   event sequence should show protected read rejection, SMP start/result,
   encryption, successful empty MIDI read and MIDI CCCD write (read/discovery/
   MTU/CCCD ordering can vary). There must be **no** `sc_sent` for this new bond.
   Capture `ble`/`log` immediately, then verify prompt-free quiet reconnect.
6. **Second iPhone, never paired:** use the never-paired phone before any bond
   is created for it. One Connect/tap and consent must establish MIDI without
   a second Connect. Expect SC Just Works, encryption, empty MIDI read and
   subscription, zero Service Changed indications, and no immediate central
   0x13 termination. Save both event readouts and test MIDI both ways. Then
   disconnect/save and verify bonded reconnect without a prompt or indication.

For either new Apple pairing, allow the >=5-second L2CAP request to run and
record its result separately from the central's final 7.5-ms/2-s or other
chosen parameters. In this firmware the MIDI CCCD is public: an early write
can succeed before pairing, but MIDI notifications wait for encryption. The
protected MIDI read is the intended pairing trigger. If the first connect
still drops, preserve the complete event sequence and central trace before
trying a second Connect; distinguish 0x13, SMP errors, negative LTK, MIC,
supervision, missing confirmation and client-side cache behavior.

Optional loss case, using the owner's established fixture: disconnect a stale
bond before its Service Changed confirmation, then reconnect without a power
cycle. Expect the indication again. Confirmation is durable after the idle
object save; a hard power cut before that save can legitimately repeat it.
