# R6 — investigation and frozen radio diagnostics

**Status: the physical MIC failure is unresolved. R6 is a diagnostics image,
not a demonstrated reliability fix.** No uncorrupted simulation failure has
been reproduced, so there is no proved divergence sequence to repair. The
requested physical failure rate, and complete per-air-TX tracing, have not been
achieved. Passing host simulation is not hardware acceptance.

Work is uncommitted above the supplied R5 state in both trees. R5's eight
fm1-base bonds, ten resolving-list entries, object format and migration remain.
No device, adapter, bench network host, flash, commit or push was used. The first
evidence read was `SOAK-R4.txt`. Sources were restricted to the supplied r8
snapshot, allowed source trees, public Core text and Apache NimBLE. Excluded
fm1-base `ble/`, `device/radio.c` and `examples/wifi/` were not inspected.

## What the supplied evidence proves

`ping-r4/mic4-log.txt` records four local MIC failures at the next *software*
RX counter. Records 2–4 have four preceding authenticated length-5 pings with
SN 0; record 1 actually has length-16 predecessors (the journal does not show
those four as pings). Every failing PDU is length 5, LLID 3, slot 0, SN/NESN 0,
with no delayed-service flag. The TX history has two encrypted length-6
responses with consecutive counters and alternating descriptor SNs.

This does not identify whether the rejected ciphertext was a repeated old
ping, a new ping after an omitted accepted packet, a wrong snapshot, or a
packet affected by a crypto/hardware fault. The journal omits all intervening
empty PDUs and CRC-valid duplicates, raw preceding statuses, TX control-byte
transitions, and the central's counter. Repeated SN 0 in the *non-empty-only*
history is not itself a sequence violation: intervening empty PDUs also change
SN, while consuming no CCM counter. The two recorded TX loads do not prove
which of them actually went over the air or was acknowledged.

## Request/response path audit

The following is the implemented normal sequence, not an observed failing
sequence. Let C be the next central encrypted counter and P the next peripheral
encrypted counter. The one-bit sequence numbers are independent of C and P.

1. The central encrypts a new `LL_PING_REQ` at C, once. Retransmissions retain
   its payload/ciphertext, LLID and SN. Its NESN acknowledges the peripheral's
   last PDU independently of whether this central PDU is new or repeated.
2. Hardware presents RX result 1 for an accepted new packet. The HAL snapshots
   both held descriptors before releasing either DMA slot; it services the
   older slot first. With insufficient stream capacity it retains the full
   slot and its pressure state. Results 5/13 fail CRC; result 9 denotes a
   CRC-valid repetition under r8's measured interpretation.
3. `ble_ll_rx_slot()` updates timing for valid new/repeated packets. Only a
   result-1 non-empty PDU enters the ordered stream. Empty PDUs, repetitions,
   CRC failures, and unsupported status/length classes consume no RX counter.
   A held slot is never delivered twice by the recovery path.
4. The executor's `security_receive_inner()` takes C once, advances the software
   RX counter, authenticates/decrypts, then stages the control PDU. Authentication
   precedes dispatch. Later PDUs cannot overtake the staged control or its
   refill work. On a failed MIC it closes with 0x3D; there is no counter guessing
   or retry against another nonce.
5. `control_plain()` validates the ping's one-octet length and takes the default
   unsupported-opcode branch: queue `{LL_UNKNOWN_RSP, LL_PING_REQ}`. An unrelated
   pending control reply has its own TX queue entry and insertion order. Queue
   exhaustion is counted by `ctrl_drop`; it does not consume an encryption
   counter or overwrite the already loaded reply.
6. `tx_refill()` defers the work into bounded executor items. `bb_refill_limit()`
   excludes competing CPU refills, takes a stable TX-toggle/control snapshot,
   releases acknowledged buffers, and picks control before ACL. It prepares
   ciphertext once, at P, then increments the TX counter. It preserves the
   descriptor's hardware-managed SN and publishes by clearing the TX control
   byte last. Two loaded PDUs use preparation order for counters.
7. A loaded buffer remains owned by hardware until its control byte indicates
   acknowledgement. Neither retransmission nor a lost response re-encrypts it.
   A later refill releases the same queue entry once. Encryption boundary
   replies are released only by ordered RX processing; this is immaterial
   during a stable encrypted ping session.
8. After a good CRC, ACK state and RX SN state progress independently. Losing
   a peripheral response makes the central repeat its ping; that repetition
   must not decrypt again. Losing a central ACK keeps the peripheral response
   pending; receipt of the next good central PDU can acknowledge it even when
   that PDU's payload is a repetition. A CRC failure changes neither ARQ state.
   MD controls event continuation, not nonce allocation.

The RX queue, staged control, reply ordering and immutable encrypted TX buffer
have no identified software path that violates these invariants when the
hardware contract holds. A reply prepared after an automatic empty transmission,
then acknowledged in a later event, is explicitly represented in the new model.
The model also covers both RX slots held, refused storage, retransmission,
CRC errors, and delayed service. It does not reproduce the bench failures.

## Independent central and proof limits

`tests/ble_ping_stress.inc` runs a central with its own two ARQ bits, immutable
outstanding PDU, encrypted TX counter and authenticated RX counter. It uses a
100 ms APTO and starts a ping after 75 ms without authenticated peripheral
payload. It waits for the actual UNKNOWN response, sends real L2CAP/ATT Write
Commands with MIDI notes, authenticates every new peripheral non-empty PDU,
and checks delivered ATT payloads against an independent FIFO. The peripheral
baseband model in `ble_mic_radio.inc` implements the opposite ARQ machine and
an immutable outstanding automatic empty PDU. Fresh payload selection, ACK
processing and CRC rejection are separate operations; TX slots are not simply
acknowledged en masse as they were in the previous test.

The central's CCM oracle calls public TinyCrypt AES directly, separately from
controller CCM and its AES-port API. Cross-checks cover both directions, all
1–27-octet plaintext lengths, counters above 2^38, and altered MIC rejection.
The simulator injects CRC-invalid receptions and reception/ACK loss without
changing the valid ciphertexts. It checks ciphertext identity on repeats.
Random slot/executor delays cause coalescing and storage refusal. At these
modeled scheduling/slot latencies the ping rates are about 9.41/s at 15 ms and
6.56/s at 30 ms, below the bench's 12.7/s; these runs are not a rate-matched
reproduction. The inherited stress resets its own random seed independently. There are
5 M events at 15 ms and 5 M at 30 ms, plus the inherited independent-central
10 M-event stress. No failure probability has been tuned to the observed rate.

A zero-failure result cannot prove the unknown silicon behavior. In particular
r8 Part 2 §§3.4.4.1, 4.2, 4.3, 8.5 and open questions P3-3/P3-4/P3-5/P3-7 do
not establish all of these conditions:

- Whether result 9 is always an RX-SN duplicate when RX SN and TX NESN have
  different relationships; whether a newly accepted PDU can be acknowledged
  but omitted from the descriptor/interrupt stream.
- Whether TX control=empty can coexist with an automatic empty PDU awaiting
  ACK, and how a software load during that state interlocks with ACK release
  and slot alternation. The model retains the old automatic empty separately.
- DMA/header sampling, slot prefetch and TX-toggle changes while CCM is running
  or between the final header read and publication. CPU IRQ exclusion does not
  freeze the baseband. The current model advances DMA at packet boundaries,
  not inside individual CPU stores or an AES operation.
- Ownership/release after CRC failure, a full/refused RX slot, event close, or
  NESN mismatch in cases not exercised by the r8 probe measurements.
- Real AES block/FIFO faults or RF behavior that leaves a valid CRC but invalid
  authenticated data. Host sanitizers cannot model those.

For example, *if* a newly loaded encrypted response could be released by an ACK
for an older automatic empty in the same slot, the next response would use P+1
while the central still expects P. Conversely, *if* a previously accepted ping
were presented again as result 1, the worker would use C+1 on ciphertext made
with C. These are specific conditional mechanisms consistent with the missing
observations. They are **not** established events, are not injected into the
passing model, and do not together constitute a proved shared cause. No counter
rollback, alternate-counter MIC retry, speculative TX serialization or feature
change has been installed.

## Diagnostics implemented and their limits

fm1-base now retains the last **32 active radio/descriptor events**, up to
64 metadata rows per event. Felucca retains **16 active events**, up to **6**
rows each, to fit the synthesizer RAM budget. Both use diagnostics-only rings. Silent event closes
are not allowed to evict the final exchanges while supervision times out.
The first local 0x3D or 0x08 freezes the ring until power-off, across disconnect,
reconnect and HCI Reset. Read it with `fm1_remote.py log`; Felucca's console
uses the same formatter. The existing four-record MIC journal remains.

Rows contain no payload, key, IV, random material, or memory pointer. They
include event/epoch, CPU tick, slot, logical TX buffer id, raw status/control,
length, LLID, SN, MD and RX NESN. RX rows include empty and repeated descriptors,
CRC outcomes, acceptance class, and the exact counter actually attempted by
the executor, including a failed attempt. Non-decrypted rows say `ctr none`.

The `decision` values are 1 accepted/new pending or empty; 2 duplicate (no
counter); 3 CRC failure (no counter, not acknowledged under Core); 4 other
status/length; 5 authenticated; 6 MIC failure. Raw RX status bits allow the
reader to check this classification independently. Flag bits remain the r8/R4
pressure/both-full/recovery flags. For delayed service the event is the service
sample, not a claimed original RF event; the MIC journal retains its explicit
unknown-event marker.

TX `prep`, `load`, `ack` and `tx_state` rows show the two slots and their raw
control/toggle state. Load/ACK rows carry the assigned counter and buffer id;
ACK means the documented TX control-byte release was observed. The ring captures
both sides of CCM preparation, allowing a toggle change during preparation to
be recognized. No live DMA header is modified by tracing.

**This does not meet the request for every actual transmitted PDU's NESN and
new/repeated/acknowledged classification.** The approved specification supplies
no per-TX-completion stream or on-air TX NESN. The formatter therefore says
`radio_tx_source descriptor_only nesn_unknown air_new_repeat_unknown` and prints
TX NESN `?`. Stale headers in empty slots remain raw descriptors and must not
be read as transmitted payloads. A packet refused while both RX slots are full
also has no readable descriptor under r8; such an on-air reception cannot be
reconstructed. `overflow` counts rows omitted after an event's row bound;
any nonzero overflow means that event is incomplete. High burst rates or many
recovery observations can hit that bound. The 16-event requirement is met
for retained active events, but arbitrary full per-air-PDU coverage is not claimed.

Directed tests check 32 retained events in fm1-base and 16 in Felucca, empty/repeated/CRC-failed RX records,
the exact failed counter, load/ACK once, survival across Reset/reconnect, and
preservation through 559 silent closes before a supervision freeze. The shared
USB formatter's rows fit the 187-character frame limit. Hardware logging cost
and an 8-second complete `fm1_remote.py log` transfer remain owner checks.

## Proposed r9 items (not changes to frozen r8)

1. **Resolve the ownership contract before a reliability fix:** measure TX
   automatic-empty-to-loaded handoff while the empty is unacknowledged; snapshot
   TX toggle/control/header before prepare, at publish and on each subsequent
   RX. Exercise new RX SN with both acknowledging and non-acknowledging NESN,
   CRC failures, and one/both held slots. Specify what constitutes an air TX
   and when TX control may become empty. Clarify the class-C table: duplicate
   RX SN does not imply non-acknowledging NESN. Core §4.5.9 processes the two
   independently on every CRC-valid PDU; the measured same-SN/same-NESN probe
   cannot establish that all result-9 packets leave TX untouched. The production
   driver already checks TX control release independently; this is a contract
   clarification and missing stimulus, not a proven controller repair.
   Identify a supported way to observe
   actual TX NESN/retries, or explicitly narrow the diagnostic acceptance test.
2. **Retain metadata around encrypted failures:** require RX empty/duplicate
   and raw hardware status evidence, counter-to-buffer association, and a
   frozen pre-timeout history. State descriptor-only limitations explicitly.
3. **LE Ping support is not justified as this fix.** Core Vol 6 B §5.1.8 permits
   ping to an unsupported peer; the old UNKNOWN response is intentional. Full
   support would also require the authenticated-payload timeout behavior and
   HCI surface (Core Vol 6 B §§4.6.5, 5.4; HCI read/write APTO), not merely a
   one-byte response or an advertised bit. No feature is silently enabled.

Core references: [Core 5.3 official document landing page](https://www.bluetooth.com/specifications/specs/core-specification-5-3/),
[public Core link-layer text, §§4.5.6, 4.5.9, 4.6.5, 5.1.8, 5.4](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/low-energy-controller/link-layer-specification.html).
The r8 text supplies the 5.3-specific hardware interpretation; the public 5.4
HTML was used to verify ARQ/flow-control rules and ping's applicability to a
peer without the feature. No claim of full Core conformance is made.

## Validation and artifacts

All final jobs finished successfully. Detailed counts follow the artifact notes.
Build command (diagnostics default on):

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
 AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
 python3 tools/build.py template --open-ble
```

R6 is saved as `template/build/template-open-r6.fwsc`; hash in `SHA256-R6.txt`.
R4 and R5 hash preservation is checked by `PRIOR-PACKAGES-R6.txt`.
R6 SHA-256: `d071b0238adabfbc9777a7afdf36005c7c52de7db59c8850f30dfebb9452908e`.
`R6-felucca.patch` contains the complete uncommitted source/doc delta from
Felucca HEAD 88d426f, including the supplied R5 changes plus R6; apply to that
HEAD, not a second time over the already modified worktree. It includes the new
untracked diagnostic/simulation includes. `r6-source-record.json` fingerprints
the final allowed source files and package.
Build log `BUILD-R6.log`: source-only link, DMA bounds, MMIO policy, multiply/load
erratum and RAM code checks pass. Image 199,792 bytes; `.data+.bss` 34,584/98,304;
pool 153,084/344,064. R6's diagnostic ring adds about 50 KB of pool; diagnostics
off excludes it. USB output uses one bounded-event scratch buffer (in the fm1-base pool;
160 bytes on the Felucca console stack). The diagnostic stream token is 64-bit
so row-to-counter associations do not wrap after 2^26 active events.

Felucca final target build `out/ble-r6-build-final.log` passes: image 532,652
bytes; RAM 97,276/98,304; pool 335,732/344,064, leaving 8,332 pool bytes, above
the required 8,192. The larger first profile did not fit; an intermediate small
profile missed the headroom gate by 20 bytes. The final 16-event/6-row profile
and small stack scratch resolve both build failures. No memory gate was relaxed.
This profile can overflow within a busy event sooner than the fm1-base profile;
the record explicitly reports this rather than silently losing metadata.

`OWNER-BENCH-R6.md` gives the required order: a 20-minute 100 ms APTO ping test,
then R5 F-29, then 8 hours. These have **not** been run by the implementer.


## Final sanitizer results

- **fm1-base:** `TESTS-R6-FINAL.log` (also `TESTS-R6.log`) ends with
  `All open BLE ASan/UBSan suites passed.` This includes the platform seams,
  MIDI, storage and diagnostics-on/off cases, S1/S2/S3/S3.1, instant/update and
  shutdown tests, R5 F-29 eight-bond/overflow/retention tests, F-28 repeat pairing,
  migration/byte cuts, and the extended MIC suite. Four build-policy tests pass
  in `BUILD-POLICY-R6.log`.
- **Felucca:** `out/ble-r6-regression-final-tests.log` passes S1/S2/S3/S3.1,
  including F-28, with ASan/UBSan and halt-on-error. The final small-ring run is
  `out/ble-r6-mic-small-final-tests.log` (also copied to
  `out/ble-r6-mic-tests.log`). Earlier large-profile and superseded/cancelled
  diagnostic runs are not the final acceptance logs.
- **Each tree: 10,000,000 ping-model events, zero spontaneous MIC failures.**
  At 15 ms: 5,000,000 events, 705,930 ping requests, 705,929 received UNKNOWN
  replies, 356,540 ATT writes, 317,456 central repeats, 54,981 peripheral repeats,
  59,002 CRC-error receptions. At 30 ms: 5,000,000 events, 984,008 requests and
  replies, 335,279 writes, 316,805 central repeats, 54,098 peripheral repeats,
  58,163 CRC-error receptions. One 15-ms request is still in flight at the
  finite run boundary; it is not counted as a completed exchange or a failure.
- **Each tree: inherited 10,000,000-event stress, zero spontaneous MIC failures.**
  Original seed 0x4D494334 is reset independently of the preceding ping test.
  It accepts 2,280,220 encrypted PDUs; 256,465 retransmissions, 126,643 CRC errors,
  48,073 misses, 256,465 lost ACKs, 2,333 interval updates, zero late instants
  and zero supervision failures. Coalesced/full slots, backlog pressure and
  storage refusal are exercised. Pressure totals include the preceding ping
  run; per-model counters are labelled in the log.
- Directed negative checks still detect corrupted MICs as 0x3D, at the exact
  expected counter; malformed encrypted lengths also fail. Independent central
  CCM rejects deliberately altered tags in both directions, all payload lengths
  and high counters. RX_ON/OFF, restart, DMA-reuse and formatter tests pass.
  Frozen-ring tests pass with 32 events in fm1-base and 16 in Felucca, including
  supervision freeze after 559 silent closes and survival through Reset and
  reconnect. No payload/key material is formatted.

The inherited TinyCrypt `aes_encrypt.c`-only UBSan shift-base exclusion remains;
controller, HAL seams, host, store and tests are instrumented with ASan and
UBSan. No exclusion was added for R6 controller code. These are BLE suite
results; unrelated synthesizer/render/UI suites were not run.

Reproduce entirely offline:

```sh
python3 tests/run_open_ble_tests.py
python3 tests/open_ble_build_test.py
# In /root/fm1/Felucca-ble:
bash tests/run_ble_s1_tests.sh
python3 tests/run_ble_s2_tests.py
python3 tests/run_ble_s3_tests.py
python3 tests/run_ble_s3_tests.py --s31
python3 tests/run_ble_s3_tests.py --mic-stress
```

The delivered image is diagnostics on, untested on hardware. Outstanding work
is the actual divergence proof, a demonstrated reliability fix and a matching
physical ping acceptance run; per-air-TX metadata also requires a supported
hardware observation contract. No such success is asserted by this report.
