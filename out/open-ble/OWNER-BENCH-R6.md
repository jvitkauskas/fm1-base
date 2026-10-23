# Owner bench R6 — ping, F-29, eight hours

**R6 is an investigation/diagnostics image. Its reliability fix is unproved.**
Do not treat the simulated passes as permission to skip the ping gate. The
implementer did not contact hardware or run any of the following commands.
Image: `template/build/template-open-r6.fwsc`, diagnostics on; verify its hash
against `SHA256-R6.txt`. Preserve the R4/R5 images and bond storage.

## 1. First: 20-minute accelerated ping test

Use the same desktop/Intel `hci0` and bonded encrypted central→FM-1 MIDI setup
as R4. FM-1 must send no MIDI back. Keep other centrals off. Load R6 without
erasing bonds, then power-cycle once to start with empty volatile failure
history. Connect using the existing bond and check `enc_enabled 1`,
`nimble_bonded 1`, and MIDI subscription/input. Do not re-pair a retained bond.

Record the actual USB-MIDI and BLE-MIDI ports (`amidi -l`, `aplaymidi -l`), FM-1
address and current **ACL connection handle** from btmon. The handle is not an
ALSA port or the controller index and need not still be `0x0800`.
`ping-r4/apto.py` binds **hci0 only**, so use that adapter for this procedure.
The owner's Python environment needs the same mido/ALSA dependencies as R4.
Run from the fm1-base source checkout:

```sh
. venv/bin/activate
export FM1_USB_PORT='EXACT USB-MIDI port name'
export FM1_BLE_MIDI_PORT='EXACT ALSA BLE-MIDI destination'
export FM1_ADDRESS='FD:60:52:C0:DF:6C'  # replace if different
export FM1_HCI_HANDLE='0x0800'         # replace with this connection's handle
mkdir -p out/open-ble/ping-r6
sudo -v
```

In a separate terminal start the host-side capture before changing APTO:

```sh
sudo btmon -i hci0 -w out/open-ble/ping-r6/capture.btsnoop \
  > out/open-ble/ping-r6/btmon.txt
```

Use the supplied R6 `run.sh`, an adaptation of the original
`ping-r4/run.sh` that calls the **unchanged `ping-r4/apto.py`**:

```sh
bash out/open-ble/ping-r6/run.sh
```

It saves baseline/final full logs, sends the same notes file central→FM-1,
sets APTO to **10 × 10 ms = 100 ms**, and samples the compact readout every
30 seconds for **20 minutes**. It stops on a disconnect and saves the full
frozen log before any reconnect. Unlike the original R4 script it uses the
selected ports/handle and never automatically reconnects. Save prior `ping-r6`
readouts elsewhere before a repeat; the helper refuses to overwrite its
baseline. The first APTO readout must be exactly **`status 0x0`**. A wrong handle
or unsuccessful command is not a valid ping test.

Run at the same 15 or 30 ms interval that reproduced R4; record
`conn_interval_1250us` (**12** or **24**). If feasible repeat the full test at
the other interval, after saving the first directory. Do not change the
connection interval by disconnecting unnoticed during a timed run. R4's HCI
connection-update helper may be used separately to establish the required
interval, then obtain the new live handle if a reconnect occurred.

Exact readout to inspect after the run:

```sh
rg '^log: (mic_failures|mic_records|supervision_fail|disconnects|link_epoch|enc_enabled|nimble_bonded|nimble_subscribed|conn_interval_1250us|midi_packets_in|midi_packets_out|midi_bad|midi_drop|executor_item_over_100us|refill_over_80us|radio_frozen|radio_event)' \
  out/open-ble/ping-r6/before-log.txt out/open-ble/ping-r6/final-log.txt
rg 'MIC Failure|Connection Timeout|Disconnect Complete|Write Authenticated|Timeout:' \
  out/open-ble/ping-r6/btmon.txt
```

**PASS requires all of these:**

- `mic_failures 0`, `mic_records 0`, and `radio_frozen 0 reason 00 events 32`
  at the end (the event count can be below 32 only at the initial baseline).
- No increase in `supervision_fail`, no unexpected `disconnects` increase,
  stable `link_epoch`, and no 0x3D or 0x08 host Disconnect Complete. No
  `DISCONNECTED` in `events.txt`.
- Encrypted/bonded/subscribed throughout; MIDI input increases, output does
  not increase, `midi_bad` and `midi_drop` remain zero.
- No new `executor_item_over_100us` or `refill_over_80us`; full USB `log`
  completes without a missing-end error. An incomplete log is a diagnostic
  failure to retain, not evidence of a clean radio link.
- btmon confirms successful APTO programming. Host HCI capture does not show
  every LL ping; use the control_rx opcode-0012 work count / encrypted counter
  progress as supporting evidence. With no FM-1 output except replies, the TX
  encrypted counter should advance frequently. A 20-minute run at the R4 rate
  exercises about 15,000 exchanges; a low or stationary count invalidates the
  accelerated test.

Stop btmon with Ctrl-C and keep both its text and btsnoop file.
**If any such drop occurs, stop the acceptance sequence.** Save `drop-log.txt`,
`drop-ble.txt`, `btmon.txt`, btsnoop, the baseline and final samples. Do not reboot
before the full frozen log has been saved. The first ring stays frozen across
reconnect/HCI Reset, until power-off; later MIC records do not replace it.

For a local MIC failure expect `radio_frozen 1 reason 3D events 32`, a final
`rx ... decision 6 ctr ...`, and a matching `mic_pdu` journal row. If the central
rejects an FM-1 packet and FM-1 times out, expect freeze reason **08** and the
last active exchanges preserved through the silent timeout. Inspect `radio_event`
`overflow`: any nonzero value means omitted rows in that event. Decisions:
1 new/empty; 2 duplicate; 3 bad CRC; 4 other; 5 authenticated; 6 MIC failure.
`ctr none` means the packet was not passed through CCM. TX `prep/load/ack/tx_state`
are descriptor observations. TX NESN `?` and air new/repeat are **unavailable**;
do not turn a stale empty-slot header into an alleged air transmission.

After saving the clean result, restore the desktop's ordinary 30-second APTO
for the current live handle:

```sh
sudo python3 out/open-ble/ping-r4/apto.py "$FM1_HCI_HANDLE" 3000
```

Require `status 0x0`. If the handle changed, first find its new value. Disconnect
normally after saving the test. Do not run the stress helper during F-29 or the
8-hour soak.

## 2. Then: R5 F-29 bond capacity

Only proceed after the ping gate passes. Follow **all** of
`out/open-ble/OWNER-BENCH-R5.md`, using the R6 image throughout. Its full BlueZ
identity switching/state-preservation and restore commands remain applicable.
Save each step into `out/open-ble/f29-r6/`, not over the R5 evidence.

The required checkpoints are capacity **8**, resolving-list capacity **10**,
R4 bond upgrade without re-pair, eight mixed central identities and both CCCDs,
power-cycle and reconnect order **D,B,H,A,F,C,G,E**, then three more E sessions
with exactly zero extra programs/erases. Ninth identity I replaces only D,
replacement count rises exactly once, a second power-cycle preserves ranks,
and D's old key produces the expected negative LTK reply while the surviving
eight reconnect encrypted with their stored CCCDs. With all peers supplying
IRKs, RL occupancy is nine (eight peers plus local), within capacity ten.
Record no new MIC failures, timing overruns or unexpected resets. Restore the
original desktop identity and daemon state exactly as that document describes.

## 3. Finally: eight hours

After F-29 passes and the desktop is restored, power-cycle the FM-1 once to
start a fresh diagnostic history while retaining bonds. Save pre-run `ble` and
`log` readouts. Restore APTO to 30 seconds on the established handle (or confirm
BlueZ programs 30 seconds on reconnect). Use the original R4 load pattern:
central MIDI about every seven seconds, no FM-1 MIDI output, central updates
every ten minutes through 15/30/11.25/15 ms, USB samples every 60 seconds.

```sh
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" ble > out/open-ble/r6-before-ble.txt
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" log > out/open-ble/r6-before-log.txt
sudo -v
python3 tools/ble_overnight_soak.py \
  --address "$FM1_ADDRESS" --usb-port "$FM1_USB_PORT" \
  --midi-port "$FM1_BLE_MIDI_PORT" --index 0 --hours 8 \
  --output out/open-ble/soak-r6
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" ble > out/open-ble/r6-after-ble.txt
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" log > out/open-ble/r6-after-log.txt
```

Use the same invocation/privileges as the owner's successful R4 soak if the raw
HCI socket needs elevated access; ensure the USB/ALSA Python environment is
available to that process. The output directory must not already exist. Keep
samples, summary, btmon, btsnoop and the final full logs. Require **zero 0x3D/0x08
drops**, no new MIC journal/freeze, no late instants, MIDI bad/drop zero, no
unexpected NimBLE resets, and no audio/executor/refill budget regressions.
The soak helper may reconnect, but the first R6 ring must remain frozen if a
failure occurs; save it before powering off. Passing this bench supplies new
physical evidence; it does not retroactively establish a mechanism absent
from the R6 simulation.
