# R8B combined owner hardware bench

Status: **not run by the implementer**. This is the single acceptance checklist
for `template/build/template-open-r8b.fwsc` (diagnostics on) and
`template/build/template-open-r8b-release.fwsc` (`--ble-diag off`). Package
hashes and sizes are in [RESULTS-R8B.md](RESULTS-R8B.md) and
[SHA256-R8B.txt](SHA256-R8B.txt). Start with the diagnostics image for the full
bench. Record the package hash, FM-1 identity, central/OS/adapter versions,
MIDI app, time, and each deliberate disconnect or power cycle. This procedure
is for the owner; no device access was performed during implementation.

## 1. Capture over USB

Use the exact **USB MIDI** port, independently of BLE. From this source tree:

```sh
python3 tools/fm1_remote.py --port 'EXACT USB MIDI PORT NAME' ble > r8b-start-health.txt
python3 tools/fm1_remote.py --port 'EXACT USB MIDI PORT NAME' log > r8b-start-full.txt
```

Install `mido`/`python-rtmidi` on the owner host if needed. A command that reports
a missing end frame is an incomplete capture; repeat it. Preserve full files,
not just extracted counters. Firmware banners inherited from older rounds can
still name NimBLE 1.9; identify this candidate by its package hash (host 1.10).
Keep unused centrals' Bluetooth off so exactly the intended central connects.

## 2. First pairing: BlueZ SC, macOS, then iPhone

Perform all of steps 2–4 for each central. For this first-pair test only,
forget any previous FM-1 entry on that central. Keep all successful bonds for
the subsequent reconnect tests. Do not erase the keyboard's store.

- **BlueZ:** use an SC-capable adapter and pair with Secure Connections enabled;
  retain `btmon` evidence of SC negotiation and the public-key exchange. A
  merely successful legacy pair does not pass the SC case. Enable the MIDI
  notification subscription in the BLE-MIDI client.
- **Mac:** pair/connect in Audio MIDI Setup's Bluetooth MIDI configuration,
  then open the MIDI app/DAW. Retain PacketLogger evidence if available.
- **iPhone:** use an app's Bluetooth MIDI connection panel, pair/connect, and
  enable its MIDI endpoint. Record the actual method; Apple legacy or SC is
  acceptable, but encryption and a 16-byte key are required.

Discovery remains public; the first protected MIDI value read may return ATT
Insufficient Authentication `0x05`. **NimBLE 1.10 also requires encryption for
MIDI CCCD writes.** With an unpaired, unencrypted connection, a write of `01 00`
to MIDI CCCD `0x0011` must return ATT Error Response bytes
`01 12 11 00 05`, must not enable notifications, and must not change the CCCD
from `00 00`. Reading that CCCD is still allowed. Pair/encrypt, then retry:
the same write must return Write Response `13`, enabling notifications. The
MIDI value is `0x0010`; its encrypted initial read returns an empty value.

Exercise this subscribe-before-pair ordering with a scriptable BlueZ GATT
client on a fresh connection before SMP completes. For Mac/iPhone, retain the
natural client ordering from their trace: if either subscribes first, verify
that it handles the rejection, pairs/encrypts and retries successfully. If it
reads or pairs first, record that ordering rather than claiming the early
rejection was observed. In all cases the end-to-end app must reach encrypted,
subscribed MIDI without a reconnect loop. There is no unsolicited first-bond
Service Changed indication; GATT handles remain unchanged from R7.

After pairing/subscribing, save a full USB log and check these exact lines
(values shown are required; `<...>` denotes a recorded variable value):

```text
log: ble_diag 1
log: peripheral_linked 1
log: link_state_idle_created_up_closing_stopped 2
log: link_epoch <epoch>
log: enc_enabled 1
log: enc_rx_on 1
log: enc_tx_on 1
log: nimble_encrypted 1
log: nimble_bonded 1
log: nimble_key_size 16
log: nimble_pair_method_legacy1_SC2 <1 or 2; BlueZ SC must be 2>
log: nimble_bond_sc <0 or 1; BlueZ SC must be 1>
log: nimble_subscribed 1
log: nimble_mtu <negotiated MTU>
log: bond_capacity 8
log: bond_count <count, increasing only for a new identity>
log: bond_identity <identity> type <type> rank <rank> <SC or legacy> cccd 0010=0001
log: bond_pending 1
log: bond_writes <baseline>
log: bond_failures 0
```

`bond_identity` can also include `cccd 0008=0002` for Service Changed; the two
CCCD fields may occur in either order. These rows list characteristic value
handles (`0010`, `0008`), not descriptor handles (`0011`, `0009`). No secret
key bytes are required. Confirm `bond_pending 1` for the fresh bond before
closing the link; merely seeing pairing completion does not prove flash save.

## 3. Disconnect once, then prove persistence across power loss

A new bond or changed MIDI subscription is saved **only after the BLE link
closes**. Switching the FM-1 off during its first connected session loses the
bond; the central then fails to reconnect until it forgets the FM-1 and pairs
again. Do not intentionally trigger that known limitation during acceptance.

For each central, leave the FM-1 powered and perform the disconnect-once step:

- BlueZ: disconnect the device and prevent automatic reconnect while sampling.
- macOS: turn **Bluetooth off**. Audio MIDI Setup's “Disconnect” does not close
  the underlying BLE link and is insufficient.
- iOS: toggle Bluetooth off, wait for the USB readout below, then on again.

While the central's Bluetooth is still off (or BlueZ is disconnected), capture
a full log. Require:

```text
log: peripheral_linked 0
log: link_state_idle_created_up_closing_stopped 0
log: bond_pending 0
log: bond_writes <greater than the connected baseline>
log: bond_erases <greater than the connected baseline>
log: bond_failures 0
```

Do not re-enable Bluetooth until the pending flag clears. Re-enable it and
reconnect without forgetting or pairing again; repeat the connected-line
checks in step 2. Now power-cycle the FM-1 while this already saved bond is
connected. Reconnect from that central **without Forget or a new pairing
prompt**, and verify encryption, bonding and MIDI subscription again. Record
the identity and `bond_count` on each boot. Counters reset on boot; do not
compare `bond_writes` numerically across a power cycle. A reconnect can mark
recency/cache metadata pending again; the persistence proof is use of the
saved LTK/CCCD after power loss, not a permanently zero pending flag.

Also change the MIDI subscription, disconnect once and wait for
`bond_pending 0`; reconnect and verify the saved subscription. If the app
automatically re-subscribes, use a trace to distinguish restored CCCD state
from a fresh write. Finish with notifications enabled for the MIDI/soak tests.

## 4. MIDI in both directions on every central

Send a known repeating note-on/note-off sequence from the central to the FM-1,
then play physical FM-1 keys into a MIDI monitor or DAW on that central.
Include chords, velocity changes, and note releases. Check audible output,
received note counts/order, and no stuck notes. Save before/after USB logs:

```text
log: midi_packets_in <increases for central-to-FM-1 traffic>
log: midi_packets_out <increases for FM-1-to-central traffic>
log: midi_bad 0
log: midi_drop 0
log: nimble_subscribed 1
log: enc_enabled 1
```

## 5. 30-minute continuous soak, then overnight with the Mac

Keep the diagnostics image and Mac as central. Run a loop of matched note-on/
note-off messages in the DAW to the FM-1, with audio sounding and USB audio
recording if used in normal operation. Record FM-1 keys back into a second
track periodically; avoid a MIDI feedback loop. Keep the Mac awake and record
the sleep/display/USB settings. Run **30 minutes** continuously. If it passes,
continue for **at least 12 hours overnight**, with the **Mac as central**.
This is not the BlueZ automated overnight script. Do not schedule deliberate
disconnects or periodic resets that could mask a link fault.

Save a full log at the start and end of each phase and a compact `ble` sample
every 60 seconds. For example, on the Mac with the source tree and USB tool:

```sh
mkdir -p out/open-ble/owner-r8b-mac
python3 - <<'PY'
import subprocess, time
from pathlib import Path
port = 'EXACT USB MIDI PORT NAME'
seconds = 1800                  # use 43200 for the subsequent overnight phase
label = '30min'                  # use 'overnight' for that phase
out = Path('out/open-ble/owner-r8b-mac')
cmd = ['python3', 'tools/fm1_remote.py', '--port', port]
def capture(kind, filename):
    with (out / filename).open('w') as f:
        subprocess.run(cmd + [kind], stdout=f, check=True)
capture('log', label + '-start.txt')
start = time.monotonic()
n = 0
while True:
    capture('ble', f'{label}-{n:04d}.txt')
    if time.monotonic() - start >= seconds:
        break
    time.sleep(max(0, min(60, seconds - (time.monotonic() - start))))
    n += 1
capture('log', label + '-end.txt')
PY
```

Require a stable `link_epoch`, no extra `disconnects`, increasing
`conn_events` and both MIDI packet counters, and continuing encrypted/
subscribed state throughout each phase. Inspect these exact lines in the
start/end full logs and available compact samples:

```text
log: mic_failures 0
log: enc_phase_errors 0
log: supervision_fail 0
log: late_instants 0
log: error 0
log: failure_stage 0
log: dma_canary_errors 0
log: nimble_error 0
log: nimble_assertions 0
log: nimble_event_drop 0
log: nimble_acl_drop 0
log: nimble_heap_fail 0
log: nimble_resets <unchanged>
log: executor_selftest 1
log: executor_item_over_100us 0
log: executor_own_over_250us 0
log: executor_over_250us_plus_item 0
log: refill_over_80us 0
log: ble_critical_over_390us 0
log: audio_late 0
log: audio_xruns 0
log: audio_max_wall_us <below the 5805-us audio-half deadline>
log: executor_dispatch_live_max_us <record and compare to the existing 250-us budget>
log: worker_timer_max_gap_us <record>
log: worker_timer_over_7500us <no increase during the connected soak>
log: last_disconnect_reason <unchanged during the soak>
log: r7_background kat_runs <increasing> kat_failures 00000000 tx_checked <increasing> tx_mismatches 00000000
log: r7_cost_ticks24 steps <increasing> max <record> over100 00000000 wall_max <record>
log: r7_record rx kind 00000000 complete 00000000
log: r7_record tx kind 00000000 complete 00000000
```

R7 rows are hexadecimal, including ticks and counts; divide ticks by 24 for
microseconds. Other counters above are decimal, except the hex disconnect
reason. R7 record rows contain more fields after these prefixes. A worker-gap
maximum may already include boot, radio start or the deliberate idle flash
save; compare the connected phase's increments and `worker_gap` reason rows
rather than assigning every historical maximum to a connected write. Save the
observed interval (`conn_interval_1250us`), latency (`conn_latency`) and timeout
(`conn_timeout_10ms`); record the central's choice rather than forcing an Apple
interval. If a budget is exceeded or MIDI/audio glitches, retain the log and
mark acceptance failed/pending investigation.

On any unexpected drop, **keep the FM-1 powered** and collect two complete
`log` captures before resetting it. Save all `r7_*`, `mic_*`, `radio_*`,
`ble_event`, `link_epoch` and disconnect/timing rows, plus the central trace and
timestamp. Follow [OWNER-BENCH-R7.md](OWNER-BENCH-R7.md) to interpret the frozen
records. The earlier AX210 wrong-MIC finding is not permission to dismiss a
new Mac failure; classify the new evidence. Do not add LTK/session keys to
shared logs.

After both soaks, disconnect the Mac once more and verify `bond_pending 0`.
Finally install the release package and repeat pairing, MIDI both ways, the
disconnect-once step and the power-cycle proof with the Mac. Require
`log: ble_diag 0`. In this profile use the compact lines `peripheral_linked`,
`link_state_idle_created_up_closing_stopped`, `enc_enabled`, `nimble_bonded`,
`nimble_subscribed`, `bond_pending`, `bond_failures`, `bond_count`,
`bond_identity`, `midi_packets_in`, `midi_packets_out`, `midi_bad` and
`midi_drop`, with the values/changes specified above. Detailed `r7_*`/event
journals, key-size/method rows, and `bond_writes`/`bond_erases` are absent from
release; use the pending transition plus the saved reconnect after power loss
as its persistence proof, and the central trace for method/key size. Record
the release hash separately. Report pairing, saved reconnect,
MIDI in/out, 30-minute soak, overnight hours, USB/audio observations and release
spot-check as separate pass/fail items, with links to the raw captures.
