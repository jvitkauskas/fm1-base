# Owner-only R4 soak: eight hours again

Finish today's R3 Apple bench before installing R4. The agent did not flash or
contact any device. `template/build/template-open.fwsc` remains R3 (SHA256
`42a5b5602d93dcc0cc69cd0d1bb22e732399ebe3905eaabea70822c6b7a12eec`).

After that bench, use the diagnostics-on R4 package:
`template/build/template-open-r4.fwsc`, SHA256
`8866ab78df218f4a9c89c4b409e9fce9c3b2d4d4b49aeb2574ef94f85ed6fe06`.
R4 has no proven fix for the rare failure; the repeat soak is still required.

Keep the same desktop BlueZ encrypted SC bond and subscribe its BLE-MIDI ALSA
port. Keep the R2 traffic: notes every five seconds, connection update every
ten minutes cycling 15/30/11.25/15 ms, USB sample every sixty seconds, btmon
capture, and ordinary audio load. Do not clear bonds or reset mid-run. Close
other USB log clients. Supply the exact USB-MIDI name and BLE ALSA destination:

```sh
sudo python3 tools/ble_overnight_soak.py \
  --address FD:60:52:C0:DF:6C \
  --usb-port 'EXACT USB-MIDI INPUT/OUTPUT NAME' \
  --midi-port 'EXACT BLE-MIDI ALSA DESTINATION' \
  --hours 8 --output out/open-ble/soak-r4
```

The output directory must be new. The tool saves **raw** `usb-NNNN.txt` dumps,
`samples.jsonl`, `summary.json`, `btmon.txt`, and `capture.btsnoop`. Journal
rows appear in compact samples automatically; JSON counters alone do not
contain the multi-field MIC rows. On a drop the tool attempts an immediate
USB snapshot before reconnecting. A USB read failure is recorded separately.

Before and after the run, with the soak's USB client closed, save:

```sh
python3 tools/fm1_remote.py ble > out/open-ble/r4-before-ble.txt
python3 tools/fm1_remote.py log > out/open-ble/r4-before-log.txt
# Repeat after the run with r4-after-ble.txt / r4-after-log.txt filenames.
```

Check that encryption, bond/subscription, MIDI delivery and audio are healthy;
MIC count does not increase; supervision failures, late instants, resets,
canary errors, MIDI drops/bad packets, audio late/xruns stay zero. Record the
new item/refill/critical/dispatch maxima: R2's <=52-us work item maximum is
not an R4 timing measurement. Check the established bounds (100-us item,
250-us executor own work, 80-us refill, 390-us critical section, 7.5-ms live
dispatch gap) and retain all raw rows if any counter increments. Also retain
R3's Apple bench results separately; this BlueZ soak does not replace them.

If a MIC failure recurs, keep power on and retain every raw USB dump. The
last four failures survive disconnect, re-advertising, reconnect and HCI Reset;
a power cycle clears them. Another failure can eventually overwrite the
oldest, so collect promptly after stopping the soak's USB client:

```sh
python3 tools/fm1_remote.py ble > out/open-ble/r4-mic-ble.txt
python3 tools/fm1_remote.py log > out/open-ble/r4-mic-log.txt
```

For each `mic_record` serial, read its corresponding `mic_pdu`, `mic_rx`, and
`mic_tx` rows together:

- Correlate `ms`/`received_ms`, epoch, event or service-event sample, interval
  (1.25-ms units), and `since_update_ms` with btmon. The age is **hexadecimal**;
  `FFFFFFFF` means no connection update in that link. `ms` is detection time;
  `received_ms` is slot-service time, which may be later than reception.
- Flags are hex: `01` delayed, `02` repeat, `04` prior backlog pressure,
  `08` original event unknown, `10` both slots full, `20` executor recovery.
  With `08`, event 65535 is a sentinel and `service_event` is only the global
  event sampled at service. Do not infer an exact air event from it.
- Read raw status, slot, LLID/SN/NESN/MD and wire length, then the ten-digit
  **hexadecimal 39-bit counter used**. The failing record is independent of
  the live encryption counters, which reset on a new link.
- Check the four preceding authenticated, encrypted non-empty RX entries,
  oldest first: the failed counter should follow the last history counter.
  Counter gaps/repeated attempted counters, or a code-9 PDU reaching CCM,
  would be strong ordering evidence. Equal SN across non-empty entries alone
  is not proof of a duplicate: unlogged empty PDUs can toggle SN between them.
- The last two encrypted TX loads give SN and counter for central correlation;
  preloaded/unacknowledged packets may be included. They are not an on-air TX
  capture. Normal retransmissions retain the original counter/ciphertext.

Send the owner/reviewer the image hash, summary, **all raw MIC rows**, nearby
USB samples, btmon/btsnoop, and the raw packet containing the failure if the
central capture provides it. The records can expose ordering/counter issues;
consistent metadata alone cannot distinguish every RF/AES/hardware fault.
No journal row contains keys, IVs, ciphertext or plaintext. On D-1 failure the
FM-1 reports 0x3D and stops new transmissions; the central may still report
0x08, as in R2.
