# Owner readout after an R7 failure

The implementer did not flash or contact hardware. R7 is a diagnostics image;
it has not demonstrated a physical reliability fix or measured target audio/
CPU acceptance. R4 is the currently observed device image.

On the R7 boot, first collect a healthy log while notes/audio and 100-ms-APTO
pings are active. Require zero `r7_cost_ticks24 over100`,
`executor_item_over_100us`, `audio_late`, `audio_xruns`, `dma_canary_errors` and
background `kat_failures` / `tx_mismatches`. Check dispatch, refill, blackout,
worker-gap and TX-hold maxima before continuing a long run. This establishes
the target timing that host simulation cannot measure.

After a failure, **keep power on**. The first RX/protocol record and first TX
mismatch are independent and remain frozen across reconnect and HCI Reset.
Do not power-cycle before saving them. A further failure cannot replace an
already frozen record of the same type.

On the owner's machine, choose the exact **USB** MIDI port, so the readout does
not depend on a functioning BLE link. Run from the fm1-base source tree:

```sh
python3 tools/fm1_remote.py --port 'EXACT USB MIDI PORT NAME' log > r7-after-failure.txt
```

Save the entire file. The remote tool retries incomplete transfers and reports
a missing end frame; such an exception is not a complete readout. Once it has
returned successfully, repeat the command into a second file. The frozen record
bytes should agree (background counts and current-link health can increase).

Collect these exact line prefixes, including every repeated row:

```text
log: r7_diag
log: r7_background
log: r7_cost_ticks24
log: r7_total_ticks24
log: r7_record rx
log: r7_record tx
log: r7_pdu
log: r7_ciphertext_tag
log: r7_input_before
log: r7_check
log: r7_key
log: r7_previous
log: r7_dma
log: r7_dma_read
log: r7_dma_ciphertext_tag
log: r7_rand
log: r7_ediv
log: r7_skdm
log: r7_skds
log: r7_ivm
log: r7_ivs
log: r7_iv_used
log: r7_time_ms
log: r7_close_time_ms
log: r7_old_work
log: r7_aes_before
log: r7_aes_failed
log: r7_aes_retry
log: r7_aes_after
log: r7_end
```

`r7_record rx` and `tx` delimit their following detail rows. `kind=0` means no
record. `complete=1` means frozen and ready. Kind 1 is rejected encrypted RX,
kind 2 is a withheld TX mismatch, kind 3 is a local protocol/phase 0x3D without
a rejected ciphertext. A kind-0 record has no following details.

Also preserve these existing lines and all their repeated rows:

```text
log: sample_ms
log: link_epoch
log: last_disconnect_reason
log: enc_phase_N_S1_S2_PA_PB_PC
log: enc_rx_on
log: enc_tx_on
log: mic_failures
log: enc_phase_errors
log: error
log: failure_stage
log: dma_canary_errors
log: mic_records
log: mic_record
log: mic_pdu
log: mic_rx
log: mic_tx
log: radio_
log: ble_event
log: executor_
log: refill_
log: worker_
log: ble_critical_
log: audio_late
log: audio_max_wall_us
log: audio_xruns
log: nimble_resets
log: nimble_error
```

The trailing underscore prefixes denote all matching lines, not a literal
single row. `mic_record` is a four-entry rolling journal; R7 retains the first
record, so match epochs rather than assuming the latest journal entry belongs
to it. The R6 radio ring can have frozen earlier on an 0x08.

For a convenient extracted view **after saving the full log**:

```sh
rg '^log: (r7_|sample_ms|link_epoch|last_disconnect_reason|enc_|mic_|error|failure_stage|dma_canary_errors|radio_|ble_event|executor_|refill_|worker_|ble_critical_|audio_|nimble_resets|nimble_error)' r7-after-failure.txt > r7-readout.txt
```

All R7 fields are hex, including times and counts. Result 0 = fail/mismatch,
1 = pass/match, 2 = unavailable. A `near_pass` bit is meaningful only if the
same bit is set in `near_valid`. Bits 0..4 are C−2..C+2 in `dir`, and 5..9 are
C−2..C+2 in the opposite direction. RX dir=1, TX dir=0. At C=0, valid mask is
39C; at C=7FFFFFFFFF it is 0E7. Divide timer ticks by 24 for microseconds.
FFFFFFFF in an age/gap field means the timestamp did not exist.

Preserve the desktop's timestamp and disconnect reason, and existing btmon/
sniffer capture if available. Note whether a forced reconnect or host/controller
Reset occurred and how long since the previous link. A peer-reported MIC failure
followed only by local supervision timeout may have no complete R7 record.

Keep the desktop LTK private. Clear Rand/EDIV/SKD/IV values and ciphertext/tag
in the log are safe diagnostic inputs for independent offline verification.
Do not append the LTK or a computed session key to the shared report. Byte order
and the complete result-to-cause table are in `RESULTS-R7.md`.
