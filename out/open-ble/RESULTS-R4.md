# R4: rare MIC failure investigation (2026-10-22)

## Outcome

The R2 soak's MIC failure is **not reproduced or explained**. No defect that
explains that CCM failure was proven, and no CCM-counter algorithm was changed.
One separate, proven slot-wait bookkeeping defect is fixed: a blocked marker reservation now marks
its held slots as delayed/backpressured. R4 also adds a persistent, metadata-only
MIC journal and a production-HAL stress suite. R3 Service Changed, database generation, Apple connection behavior,
security policy, and D-1 shutdown behavior are retained.

**10,000,000 randomized connection events passed under ASan/UBSan**, with
2,280,220 accepted encrypted non-empty PDUs and zero spontaneous MIC failures.
Injected MIC corruption is detected with reason 0x3D and the expected record.
This validates the documented virtual-radio model; it does not establish the
absence of an undocumented hardware status, ACK-visibility, DMA, or AES fault.
The owner must repeat the eight-hour hardware soak.

## Inputs and boundaries

Read r7 `02-baseband.md` §§3.4.4, 4, 7, 8 and `03-link-layer-hci.md` §8;
read the supplied `SOAK-R2.txt`, all 533 JSONL rows, and `summary.json`.
The sample at 05:40:00 has MIC count 0, epoch 30, interval 24; the sample at
05:40:40 has MIC count 1, epoch 31 and the link down; reconnection reaches
epoch 32 at 05:40:46. The recorded failure is remote from the previous update.
The final encryption counters belong to the reconnected link and cannot
identify the failed packet's nonce. The cumulative CRC/repeat/delay counters
contain no packet-order reconstruction. The CRC false-accept estimate is an
argument against corruption, not evidence of a specific software defect.

Public Core references:

- [Vol 6 Part B §§4.5.9, 5.1.3](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/low-energy-controller/link-layer-specification.html): sequence acceptance, acknowledgment, and encryption start/pause.
- [Vol 6 Part E §§1–2](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/low-energy-controller/low-energy-link-layer-security.html): nonce, counter, authentication, and failure handling.

The public 5.3 HTML URLs were unavailable; the corresponding public 5.4
sections were consulted alongside r7's 5.3 requirements. New packets advance
the encryption counter; retransmissions retain their nonce. Empty PDUs use no
CCM counter. Authentication precedes delivery/LL processing but may follow the
hardware acknowledgment. A failed authentication loses the connection.

All implementation reads stayed within the allowed fm1-base, Felucca, r7,
and public Core sources. No excluded tree, closed BLE implementation, vendor
code, vendor IR, or device was accessed. The target build used the authorized
three opaque SDK packaging files for packaging and integrity checks. No flash,
commit, or push was performed.

## Receive-to-CCM walk

There is one receive service and one CCM entry, rather than a separate decrypt
path per IRQ source:

1. RX IRQ calls `bb_rx_pending` → `bb_connected_rx`. Event IRQ first drains a
   pending RX IRQ through that same function. Executor recovery calls
   `ble_port_rx_recover` → `bb_connected_rx` inside `ble_port_lock`.
2. `bb_connected_rx` observes boundary TX completions, obtains a stable
   toggle/full snapshot, orders held slots oldest first, snapshots **both**
   slots' header/status/payload, reserves space when a marker is involved,
   then publishes a replacement pointer and clears each full byte last.
   Without room, it leaves the packet owned by its full slot.
3. `ble_ll_rx_slot` (the metadata-bearing form of `ble_ll_rx_delayed`) applies
   the unchanged `ble_rx_class` filter. Codes 5/13 never queue a PDU; code 9
   refreshes valid timing/supervision but never queues a PDU. Unknown codes
   and unsupported lengths never queue a PDU. LLID 1/length 0 is discarded
   before queueing, independent of MD. Only class-A non-empty PDUs, plus
   malformed zero-length non-empty-LLID cases needed for phase validation,
   reach the ordered RX stream.
4. `ble_ll_worker` removes an entry once under exclusion, checks its link
   epoch, and calls `security_receive` once. Host-side backlog exhaustion
   leaves the encrypted entry queued; it is not decrypted speculatively.
5. `security_receive` holds the port lock through `security_receive_inner`
   and closing on failure. For RX-on/non-empty: take `rx_counter++` once,
   reject exhaustion or length <5, perform CCM once with direction 1 and
   LLID AAD, then reduce the length by four. On failure, snapshot the journal
   **before** `ble_ll_close` clears timing. There is no counter rollback,
   nonce retry, adjacent-counter search, or plaintext delivery on failure.
6. A control PDU is saved for a subsequent dispatch item, without a second
   decryption. Dispatch and its deferred refills complete before another RX
   entry or marker is consumed. ACL data moves to the host queue already
   authenticated. Old-epoch/closing entries are discarded without CCM.

## Hazard review

“Safe in model” means code inspection plus the named simulation, not a claim
about unmeasured silicon behavior. No row below proves the soak's cause; one slot-wait bookkeeping hazard is proven separately.

| Area / concrete failure sequence considered | Finding, status, and action |
| --- | --- |
| ACK loss: accept encrypted A at counter N; central repeats A with unchanged SN, possibly altered NESN/MD; naïve code attempts N+1 on A. | **Safe in model.** Hardware result 9 is filtered before the FIFO/CCM. No software same-SN filter was added: empty packets also toggle SN, so equal SN among successive *non-empty* packets does not prove duplication. Random lost ACKs/repeats and existing security regressions pass. |
| Miss or CRC failure: A is missed or reports 5/13, then its unchanged-SN retry is new; alternatively a previously accepted A is repeated after a CRC-failed attempt. | **Safe in model.** A missed packet has no entry; 5/13 is filtered before length/payload use, including garbage header length 255 and bit 8 set. Only the eventual new class-A packet consumes N; a class-C retry consumes nothing. Random injections cover both histories. Independent NESN effects/undocumented result codes on silicon remain **unproven**. |
| Two full slots contain encrypted A(N), B(N+1); serving in the opposite order makes B fail at N. | **Safe in model.** Stable toggle selects the older slot per r7 §4.2. Both are copied before either is freed. Forced same-event and multi-event coalescing, random deferred services, and payload-order oracle pass. Hardware's documented order is assumed, not re-measured. |
| Hardware fills freed slot with C(N+2) while B is still being serviced; DMA reuse overwrites A or changes B's metadata. | **Safe in model.** Decryption uses independent snapshots, not DMA pointers. Directed injection at the production HAL's buffer-return barrier yields exactly A/B/C, with counters N/N+1/N+2. |
| Backpressure/starvation: deliver A but leave its full flag set; recovery later delivers/decrypts A a second time; or skip A and enqueue younger B. | **Safe in model.** The reference's delivered-starved state is absent: no room means no decode and no flag clear. Worker recovery runs after release even without another RX IRQ. Both-full recovery and two full software tiers pass, with no double decode or payload loss. The fixed DMA buffers require no runtime replacement allocation. |
| Empty LLID-1 PDU has MD=1 between two encrypted non-empty PDUs; advancing the CCM counter for that empty creates a gap. | **Safe in model.** Empty filtering tests LLID/length, not MD or sequence bits. Random empty MD=0/1 and retransmissions consume zero counters. Empty traffic can change SN between accepted encrypted PDUs. |
| Encrypted control C is decoded, deferred for dispatch, and then decrypted again; or later ACL data overtakes an RX_OFF marker. | **Safe in model.** `control_ready` holds already-authenticated bytes; later stream consumption is blocked until control/marker dispatch completes. Mixed control/ACL and update tests pass. Both use the same direction counter. |
| RX_ON: old clear S1 response and first encrypted START_RSP occupy both slots; a global enable flag decrypts the clear response or skips encrypted counter zero. | **Safe in model.** ACK completion creates a FIFO marker; r7's length-side rule places it between the held packets. Production HAL coalescing test passes. **Unproven hardware assumption:** boundary ACK release is visible with its carrier (r7 P3-7). No speculative replacement of that rule. |
| RX_OFF/restart: encrypted terminate was received before pause response ACK, but executor decrypts after receive-off/key replacement; or first new-key packet inherits the old counter. | **Safe in model.** Marker preserves old-key/counter ordering; terminate authenticates before the marker. Normal pause resets RX at RX_OFF and TX on central clear PAUSE_RSP, then the fresh handshake starts at counter zero. Production HAL pause/restart and existing phase/epoch suites pass. |
| Delayed boundary ACK: FIFO holds 19 S1 PDUs; one slot holds the encrypted ACK carrier; marker reservation fails before the slot loop; a later service forgets that this slot waited. | **Proven bookkeeping hazard, fixed in both HALs.** The early return left `rx_delayed` clear, losing the pressure flag and potentially permitting a prompt service timestamp on a later IRQ. Reservation failure now marks every held slot delayed, without decoding/freeing it. A directed test asserts the wait flag, then exactly-once marker/PDU recovery. Removing just the new flag store reproduces the failing assertion (`REGRESSION-MARKER-PRESSURE-R4.log`); with the fix it passes. Counter/marker order was already retained. This requires an encryption boundary and does not explain the soak's established-link failure. Generic refill still leaves boundary completion pending; real ACK visibility remains **unproven**. |
| Update instant I and first new-anchor event I+1 reinitialize sequence/counter state, reorder queued entries, or substitute an event counter for the packet counter. | **Safe in model.** Instant code changes timing/connection parameters, not RX FIFO or security counters. Event IRQ drains pending RX first; executor uses stream order independent of anchors. 2,333 randomized periodic updates, existing encrypted update matrices (offset/window extremes), event/tick wraps, and immediate follow-up updates pass. Hardware anchor latches are **unproven**, with no evidence of this failure at an instant. |
| Length 1..4 on an encrypted class-A PDU; length 31 at maximum; unsupported length 32..255; reserved LLID / empty control. | **Safe within r7's profile.** 1..4 consumes exactly the attempted counter then fails/authentication record; 5..31 authenticates 1..27 bytes. 32..255 is class D, with no small-buffer read/copy or counter change. Normal phase/LLID validation follows authentication. An oversized CRC-valid frame that hardware nevertheless acknowledges could desynchronize a nonconforming sender; its actual hardware ACK behavior is **unproven**, and r7 explicitly prescribes conservative drop/no-counter behavior. No relaxation was made. |
| Reconnect/Reset: executor retains a popped packet or key/counter from epoch E and authenticates it under E+1. | **Safe in model.** Entries carry link epoch, lifecycle inhibits intake/refill, quiesces, then discards queued work. Session creation clears keys/counters/histories. Reset, reconnect, SC/legacy/RPA suites pass. Failure journal is separate from session state. |
| Executor is partway through RX CCM when RX IRQ/refill replaces the shared AES key, or AES unlock exposes a failed MIC before closing. | **Safe by reviewed exclusion.** Target `ble_port_aes_begin/end` masks IRQs for the whole AES transaction; `security_receive` has an outer lock through MIC decision and close. TX refill also locks and uses `refill_busy`; ISR/recovery producers are mutually excluded. HCI LE Encrypt/session-key/privacy consumers use the same transaction API. No other AES-register consumer was found in allowed BSP/device/open-controller source. Simulation instruments non-overlapping AES transactions, but physical FIFO/status timing and 26-us hardware deadline are **unproven** in this round. |
| TX retransmission uses a fresh counter/key or two-slot loading reverses the encryption order. | **Safe in reviewed/model paths.** CCM runs at load once; ciphertext remains owned by hardware until ACK. Stable TX toggle/full ordering selects loads, and repeat traffic does not call prepare again. Last-two encrypted-load metadata is now retained. Empty hardware TX does not consume a counter. Actual RF publication timing remains owner evidence. |
| Failure evidence disappears during disconnect/session creation. | **Proven diagnostics gap, fixed.** R2/R3 retained totals, not the failed packet/counter context. R4 snapshots the requested metadata before teardown into a separate four-entry journal; stop, reconnect, and HCI Reset preserve it. Directed corruption, journal wrap, and actual USB/CDC formatter tests pass. |

## Diagnostics

In diagnostics builds, every `security_auth_failure(1)` attempt now captures
`ble_mic_record_t` through the single RX authentication failure branch. There
are four records, with monotonically increasing serials and oldest-first
readout. A fifth failure replaces the oldest; power-off clears RAM. Records
survive disconnect, re-advertising, new link creation, HCI Reset, and ordinary
stats reset. No flash writes are added.

`fm1_remote.py ble` (compact) and `log` (full) include:

- `mic_records`: retained count; `mic_record`: serial, failure ms, link epoch,
  RX-service ms, event/sample, interval, and age since the last connection
  update's nominal instant.
- `mic_pdu`: slot, flags, raw status, LLID, SN, NESN, MD, wire length, and the
  exact 39-bit RX counter used. Counter is ten hex digits; age is eight hex
  digits (`FFFFFFFF` means no connection update in this link).
- `mic_rx`: last four authenticated encrypted non-empty RX PDUs before the
  failure, oldest first, with SN/counter/wire length. The failed PDU is not
  added to this history. Empty PDUs have no CCM counter and are omitted.
- `mic_tx`: last two encrypted TX **loads**, oldest first, with descriptor SN,
  counter and wire length. A loaded packet may still be awaiting transmission
  or ACK; retransmissions do not create another history entry.

Flags: `01` delayed, `02` repeat, `04` previously blocked by backlog pressure,
`08` original air event unavailable, `10` both slots full, `20` executor
recovery. Hex bitmask. Correct class-C filtering means the failed PDU's repeat
flag should be zero. A set repeat flag/status 9 reaching CCM would be abnormal.
Slot `255` means a caller without DMA slot metadata (test/legacy API).

For delayed reception r7 provides no exact original event/timestamp per slot.
The record honestly reports event `65535` with flag `08`, plus `service_event`
(the sampled global event). Flag `08` disambiguates this sentinel from the
valid event number 65535. `received_ms`, interval, and update age are sampled
at RX service, not invented air-reception values; `ms` is failure detection.
The existing lower-bound supervision stamps are unchanged. Update-age state
uses monotonic milliseconds, not the wrapping 24-MHz timer, and the directed
170,040-ms case crosses a timer wrap. Each logical journal row fits the USB
frame; readout freezes all journal records before yielding to USB/host work.

The journal contains only timing, headers/status, counters and lengths. No
keys, IVs, ciphertext or plaintext are copied into it or formatted by it.
The formatter is also available in Felucca's CDC `ble` status output.

## Simulation and validation

`tests/ble_mic_radio.inc` supplies a virtual register bus. The runner extracts
`bb_connected_rx` and `bb_refill_limit` **verbatim** from the target HAL and
compiles them with the actual controller, security procedures and CCM engine.
An independent central preserves SN/ciphertext/counter across retries; the
virtual baseband supplies r7 result codes, toggles, full flags, and TX ACK
visibility. The ACL oracle checks byte-for-byte, exactly-once delivery.
Hardware classification and undocumented latch timing remain model inputs.

Default seed `4d494334`, 10,000,000 events (see `STRESS-R4.log`):

| Observation | Count |
| --- | ---: |
| Accepted encrypted non-empty PDUs / final RX counter | 2,280,220 |
| ACL PDUs delivered at final drain | 2,206,629 |
| CRC-valid repeats | 256,465 |
| Injected CRC-error attempts | 126,643 |
| Both-full occurrences | 398,708 |
| Services observing backlog pressure | 1,924,187 |
| Packet attempts refused by full slots | 1,697,153 |
| Missed attempts | 48,073 |
| Lost central observations of ACK | 256,465 |
| Connection updates | 2,333 |
| Spontaneous MIC failures, late instants, supervision losses | 0 |

The periodic update cycle is 15/30/11.25/15 ms. The run includes event-counter
wraps and many 24-MHz timer wraps, CRC errors, lost ACKs, repeated SN, variable
NESN/MD headers, deferred services across events, bursts, and forced long host
backpressure. Random ACK state is a model input, not a hardware observation.

Directed cases additionally verify: corrupted MIC with good simulated CRC at
the backpressured tail; exact failing counter and last RX/TX histories; four
short non-empty lengths; oversized and CRC-garbled headers; DMA reuse between
both-slot handovers; actual RX_ON/RX_OFF coalescing; old encrypted terminate
before pause marker; new-key restart at zero; four-record overwrite and
persistence; marker-reservation pressure recovery; and actual USB/CDC row formatting. `R4-MIC-FINAL.log` records a
short run plus all final directed cases, including event 5676 at 30 ms,
170,040 ms after an update, failing exactly the deliberately corrupted counter
2. The final aggregate runner includes the full 10M-event suite too.

Commands (all offline; no device access):

```sh
CC=/root/fm1/fm1-base-open/out/open-ble/cc-sanitize \
 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
 python3 tests/run_open_ble_tests.py
python3 tests/open_ble_build_test.py
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
 python3 tests/run_ble_s3_tests.py --mic-stress
```

`TESTS-R4.log` covers seams, health/advertising diagnostics on/off, offline USB
and soak tools, S1, S2, S3, S3.1, object-store, diagnostics-off integrations,
and the long MIC suite. Existing R3 GATT/SMP/Service Changed client cases pass.
`BUILD-POLICY-R4.log` covers source-only selection, MMIO exclusions, diagnostic
flags and forbidden-archive rejection. TinyCrypt's existing upstream
shift-base sanitizer exclusion is retained only for its AES implementation;
controller/HAL/seam code remains ASan/UBSan instrumented.

Felucca: the reservation-wait fix and shared R4 diagnostics are applied uncommitted to `firmware/ble/ble_link.*`,
`ble_security.{h,inc}`, `firmware/hal/fm1_ble.h`, the CDC formatter, and matching
test sources/runner. The tiny diagnostics macro header is added locally.
**No R3 host/Apple changes were ported.** Source patch:
`R4-felucca.patch`. Its S1, S2, S3 and S3.1 suites pass under ASan/UBSan;
`out/ble-r4-mic-tests.log` adds a 10,000-event port run with all directed cases
and the actual CDC formatter. The 10M run is on fm1-base's shared implementation.

## Build, artifacts, and preservation

R3 package was copied to `r4-baseline/template-open-r3.fwsc` before editing.
`tools/build.py --package-name BASENAME.fwsc` now allows direct packaging under
a separate name. Thus the canonical R3 package was **never overwritten**, even
transiently; no copy-back was needed.

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
 AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
 python3 tools/build.py template --open-ble --package-name template-open-r4.fwsc
```

- R4 diagnostics-on package: `template/build/template-open-r4.fwsc`, 610,136 B.
  SHA256: `8866ab78df218f4a9c89c4b409e9fce9c3b2d4d4b49aeb2574ef94f85ed6fe06`.
- R3 protected package: `template/build/template-open.fwsc`, unchanged SHA256
  `42a5b5602d93dcc0cc69cd0d1bb22e732399ebe3905eaabea70822c6b7a12eec`.
- R4 diagnostics-off validation package: `template/build/template-open-r4-no-diag.fwsc`,
  SHA256 `4da4d456aefb6e58b6c2fded86a4659666e2e1b40544db2bd9478b327bde6e76`.
  Use the diagnostics-on image for the next soak.

Diagnostics-on build: app 195,648 B (+1,584 from R3), static data+BSS 34,584 B
(+20), pool 101,212 B (+1,216), RAMTEXT code 3,112 B, trace 20,480 B.
Diagnostics-off: app 184,624 B, static 33,756 B, pool 99,228 B, RAMTEXT 3,112 B,
trace 0; the new journal/history/queued metadata allocate no RAM in this mode.
`BUILD-R4.log` and `BUILD-R4-DIAG-OFF.log` pass source-only link, RAM-only flash
code, multiply/load erratum, no soft double, DMA bounds, footprint and MMIO
checks. Diagnostics-on `.bin`, `.map`, and `.symbols` are retained under
`template-open-r4.*`. These are build observations, not new device timing data.
The diagnostics cost and R3 Apple behavior still require the owner bench.

Owner procedure and journal interpretation: `OWNER-SOAK-R4.md`.
