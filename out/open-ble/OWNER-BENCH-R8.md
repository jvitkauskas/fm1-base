# R8 owner bench — connected bond durability

No hardware was contacted by the implementer. Use the diagnostics candidate
`template/build/template-open-r8.fwsc` first; repeat with
`template-open-r8-release.fwsc` after it passes. Keep R4–R7 packages.

1. With the keyboard on USB, collect the baseline over its **USB MIDI port**:

   ```sh
   python3 tools/fm1_remote.py --port 'EXACT USB MIDI PORT NAME' ble > r8-before.txt
   ```

2. Pair the Mac in Audio MIDI Setup. Keep its BLE connection open, play notes,
   and collect another readout after two seconds. Do **not** click Disconnect
   or turn Bluetooth off to make the save happen. Check these exact lines
   (the tool prefixes each with `log:`):

   ```text
   log: peripheral_linked 1
   log: link_state_idle_created_up_closing_stopped 2
   log: enc_enabled 1
   log: nimble_bonded 1
   log: bond_pending 0
   log: bond_failures 0
   log: bond_sup_risk 0
   log: bond_write_link_losses 0
   log: audio_late 0
   log: audio_xruns 0
   log: dma_canary_errors 0
   ```

   `bond_count` must include the Mac; `bond_writes` and
   `bond_connected_writes` must increase while `link_epoch` stays the same
   and `disconnects` does not increase. Record the actual numbers on these
   exact line names:

   ```text
   log: bond_save_us_last ...
   log: bond_save_us_max ...
   log: bond_erase_us_max ...
   log: bond_program_us_max ...
   log: bond_stall_us_max ...
   log: bond_missed_events ...
   log: bond_sup_margin_us_min ...
   log: conn_interval_1250us ...
   log: conn_timeout_10ms ...
   ```

   Require `bond_missed_events 0` in good RF and a short guard stall (target
   <=20 us), with positive supervision margin comfortably above two
   connection intervals. The erase/save times are elapsed busy times, not
   IRQ blackouts. Keep audio/USB active through a new pairing and require
   no audible interruption, stuck notes, or lost/delayed MIDI traffic.

3. Leave the Mac connected and power-cycle the keyboard. Reconnect from the
   Mac **without forgetting the device**. It must encrypt and play MIDI
   without a new pairing prompt. `bond_count`/the Mac's `bond_identity` must
   survive; require `ltk_negative 0` in the full `log` readout and no fresh
   pairing event. Write counters reset on boot; do not compare their values
   across the power cycle.

4. Close the Mac's link when switching centrals (one simultaneous link is
   supported). Pair the iPhone with a BLE-MIDI app, leave it connected, and
   repeat steps 2–3. Keep both saved bonds, then reconnect each in turn
   after another power cycle. Neither should ask to pair again.

5. On a connected bonded central, change the MIDI notification subscription.
   Read again after six seconds: `bond_pending 0`, `bond_connected_writes`
   increased, the same epoch and no disconnect. Power-cycle and check that
   the subscribed value is restored (`nimble_subscribed` and the corresponding
   `bond_identity` CCCD metadata). A central may explicitly rewrite its CCCD
   on reconnect; retain the pre-reconnect bond metadata as evidence.

If any check fails, keep power on and save complete `ble` and `log` readouts.
R7's frozen MIC diagnostics remain present in the diagnostics candidate.
Record the package SHA-256, central, interval/timeout, and elapsed wait.
Do not interpret a power cut **before** `bond_pending 0` as a committed save.
