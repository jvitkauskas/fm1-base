# R7 — frozen encryption failure classification (fm1-base only)

R7 is a diagnostics image, **not a demonstrated fix for the physical drop**.
No device, adapter or bench host was contacted; nothing was flashed or committed.
The device evidence remains R4. R5, R6 and R7 have no new hardware acceptance
from this work. R4–R6 packages are preserved; hashes are in
`PRIOR-PACKAGES-R7.txt`.

The first evidence read was `PING-R4L.txt`, followed by
`ping-r4L2/mic1-112758.txt`, then `SOAK-R4.txt` and `RESULTS-R6.md`.
The long run's one failure was the first encrypted central PDU, counter zero,
event 7, on a reconnect 131 ms after the preceding disconnect. It is distinct
from the earlier mid-link ping failures. No common cause has been established.

## Implementation and scope

All changes are in fm1-base. Felucca-ble's worktree is unchanged: no additional
RAM or diagnostics are introduced there. `FM1B_BLE_RELIABILITY` defaults to 1
only for `FM1B_BLE_OPEN && FM1B_BLE_DIAG && FELUCCA_BLE_STAGE >= 3`.
The host-only R7 radio simulation selects that profile explicitly. Diagnostics
off excludes the implementation, retained keys, payload records, software CCM,
DMA pinning, periodic KAT, epoch tags and additional timing statistics.

The implementation is in `open_ble/ble_reliability{,_ccm,_dump}.inc` and
`ble_reliability.h`, integrated with `ble_security.inc`, the LL worker and the
open HAL. It uses the already bundled Apache NimBLE TinyCrypt AES directly;
it does not use the hardware AES port for software checks. The independent
central CCM oracle remains separate from both controller CCM implementations.

The first rejected encrypted RX PDU and the first TX hardware/software mismatch
have **separate frozen records**. A local phase/boundary 0x3D without a rejected
ciphertext is explicitly kind 3. Records survive disconnect, reconnect, HCI
Reset, radio off/on and diagnostic-counter resets. Only power-on initialization
clears them. The existing four-entry MIC journal and R6 descriptor ring remain.

### First local failure

* The exact attempted counter, raw status/header, service event, slot and epoch
  are captured. The RX DMA full flag remains set until normal authentication
  finishes. On a failure it stays set through all diagnostic checks; repeated
  recovery visits cannot enqueue that pinned descriptor again.
* The working ciphertext is copied before the original CCM attempt and again
  after its failure. The original DMA buffer is read, using volatile loads,
  at failure and after the checks. The record reports validity and all relevant
  equalities, including a changed working copy. These are actual retained DMA
  buffers, not reads of a slot already returned to hardware.
* Hardware CCM is retried at the original counter/direction. A hardware AES
  known-answer test follows immediately in another executor item. Software
  recomputes the session key if its inputs are still available, then tests
  C−2 through C+2 in both directions. Invalid counters are omitted, without
  unsigned wraparound. The original C result is `sw_same`.
* Software additionally tries the most recent disconnected link's actual
  session key with the current IV, then with that previous link's IV, at C and
  the original direction. A disconnect replaces this comparator, including
  making it unavailable if that most recent link never derived a key. A
  refresh within the current link does not replace it.
* The session key in use is compared with a private copy made at derivation.
  The IV in use is compared with the retained clear exchange. The diagnostic
  LTK and SKD derivation inputs are wiped after authenticated RX and a verified
  encrypted TX preparation have both succeeded, and at disconnect/Reset.
  Thus software derivation is available for a first-PDU failure; it deliberately
  reports unavailable for established links after that wipe. The derived-key
  comparator remains until disconnect. All private software schedules and
  temporary plaintext are wiped after use/cancellation.
* Raw AES `CON`, busy/epoch bookkeeping, operation/block counts and timeout
  counts are captured before the failed operation, after it, after retry, and
  after classification. `busy` is the driver's transaction flag, not an
  undocumented silicon busy register. **KEY/DATIN FIFOs and ENCRES/DECRES are never dumped**:
  these are data-bearing registers and can contain secret key derivation output.
* The record contains Rand, EDIV, SKDm, SKDs, IVm, IVs and IV actually used.
  These are clear on-air values. It contains no LTK, session key, key checksum,
  plaintext or expanded software key schedule.

Checks do not dispatch or accept the failed PDU, roll back/advance/guess the
live counter, or rescue a link. The original failing attempt consumes its
counter as before. All trials use private state. The controller then closes
with 0x3D. Stop/Disconnection Complete remain scheduled after the separate
classification item. A synchronous test/startup Reset helper also drains an
in-progress classifier; the real HCI Reset remains incremental.

### Background verification and scheduling

The periodic hardware KAT is due approximately every second. It runs in the
LL executor, only when the baseband busy bit is clear and at least 60 us of
executor budget remains. It has priority over ongoing software TX preparation,
so sustained TX cannot starve it. Mismatches are lifetime counters.

For every encrypted TX, software CCM first computes the exact expected
ciphertext/tag in short executor steps. No counter is consumed and no DMA slot
is selected during that work. The normal refill subsequently performs hardware
CCM, compares every ciphertext/tag byte, and publishes only a matching result.
This preserves counter allocation and publication in the same refill operation,
including when automatic empty packets move the TX toggle during software
computation. A mismatch is counted, frozen and never published; it runs the
same classifier before 0x3D closing. A software-only preparation canceled before
hardware CCM has produced no hardware output to check.

There is one software key expansion or AES block per diagnostic executor item;
result handling and large cleanup are separate items. Normal radio/AES critical
sections never span software AES. Reset diagnostic cleanup is also a separate
item. The TX MIC history now takes the queue entry's actual counter at load,
rather than the most recently prepared packet's global metadata.

## Timing and CPU cost

`r7_cost_ticks24` reports lifetime step count, maximum own CPU time, count at or
above 100 us, and maximum wall time. `r7_total_ticks24` accumulates own CPU ticks
and gives maximum TX precomputation-to-check latency. One tick is 1/24 us.
The existing `executor_kind reliability`, `control_rx`, `refill`, dispatch,
blackout, worker-gap and audio counters remain available. R7 totals measure
its executor steps; the ordinary RX/refill counters also include the added
snapshot/comparison overhead. Own time subtracts accounted audio/radio
preemption; wall time deliberately includes it.

The directed timing model charges **75 us/software AES block, 20 us/software
key expansion, one tick/copied or wiped byte, one us/hardware block**. Its
maximum diagnostic item is 2,168 ticks = **90.33 us**, with zero items at/above
100 us. A simulated 186-us audio preemption increases wall time while leaving
own time below 100 us. The inherited pessimistic Reset model uses three
ticks/byte, and control/refill uses eight us/hardware block: the R7 profile
passes its existing 70-us item limits without relaxing those limits.

**These are model bounds, not measured silicon CPU or audio results.** Target
code instruments both costs. The no-device restriction prevents measuring
software AES duration, real interrupt latency, audio xruns, or maximum DMA hold
time here. Before a long owner run, require zero `r7_cost_ticks24 over100`,
`executor_item_over_100us`, `audio_late` and `audio_xruns`, and inspect dispatch,
refill and TX-hold maxima. Passing host ASan does not prove those hardware facts.

## Reconnect review (including a 131-ms modeled reconnect)

| State | What is reset or retained | Conclusion / remaining limit |
|---|---|---|
| LL encryption, counters, key, LTK, flags | `security_new()` cancels link entropy and securely clears the entire security object at stopped-link cleanup and new creation. Only lifetime counters survive. Key/IV derivation completes before START_ENC_REQ is queued. RX_ON is an ordered marker after its acknowledgement. | No demonstrated old-key, counter or start-order path. R7 separately checks a derivation copy, software derivation, IV and previous-link key. |
| LL RX/control/refill/TX work | Close removes deferred control/refill/meta state; stopped-link cleanup flushes TX and RX backlog; creation clears indexes and per-link fields. Queue entries carry epochs and TX/RX consumption checks them. R7 pending software work is canceled/wiped. | No active LL job can legitimately cross into the new link. Inactive backing bytes and the monotonically increasing TX order remain; state/count/epoch gates make them unreachable. |
| AES operations | Begin/block/end are synchronous under the same IRQ exclusion; no DMA, callback or asynchronous AES completion queue exists. Software AES is executor-owned and cancelable. | No pending old hardware operation is established. R7 records busy/owner epochs, timeout counts and old-work masks. Retained raw result/key-expansion registers are **not** proven physically cleared; every new operation reloads its full key and block. KAT/retry diagnose faults without guessing a reset recipe. |
| RX/TX DMA and descriptor state | Stop waits for hardware busy to clear, masks/ACKs sources and removes the instance before reclaim. Timeout quarantines it. Advertising startup zeroes the complete baseband DMA area in bounded steps and republishes the frame. Connection setup clears RX ownership, stamps/delays, TX ids, TX lengths/SNs, RX toggle and event state. Each actual TX load writes its pointer/data/header and publishes control last. | No proved CPU stale-buffer reuse path. Silicon prefetch, speculative DMA and automatic-empty ownership remain the R6/r8 hardware-contract limits. R7 pins RX and checks output before TX publication to collect evidence. |
| Resolving list | Entries intentionally persist across disconnect. The per-link identity, resolution index/done/result and peer address are cleared on creation. Resolution completes before Connection Complete. HCI Reset clears/repopulates the list. | Persistence is required, not stale per-link security state. Existing F-28/F-29 cover changed IRKs, RPAs, restored bonds and overflow. Memory corruption of persisted contents is not proven absent. |
| NimBLE bonded-key restore | Gap disconnect invokes SM connection-broken cleanup. A reconnect creates a new restore procedure. `ble_sm_retrieve_ltk()` reads our security record by resolved peer identity, checks EDIV/Rand, then sends its LTK via HCI. The open store copies that identity's record. | No cached LL session key is restored by NimBLE. The controller self-check proves derivation from the **supplied** LTK, not equality with the owner's desktop LTK; offline verification supplies that independent check. |
| Queued HCI commands | The four-command FIFO is not cleared on ordinary disconnect. Historically it had no epoch tag. LTK reply acceptance checks handle, live state, S1/waiting and reply-pending, using a constant handle. R7 adds diagnostic ingress epoch tags. | **Not proven isolated by epoch.** A harness can retain an old queued LTK reply across a reconnect; the old-work masks report it. Normal synchronous NimBLE HCI credits/order have not been shown to produce this ordering on hardware. No speculative command-dropping fix was installed. An ingress tag also cannot identify a reply the host generated late after new creation. |

The old-work `create` mask samples before clearing old link state; `enc` samples
at LL_ENC_REQ dispatch against the current epoch. Bits: 0 TX queue, 1 RX stream,
2 staged control, 3 driver AES transaction busy, 4 software crypto job, 5 deferred refill
(create only), 6 security key/random/LTK-event work (create only), 7 HCI command,
8 HCI LTK reply. At creation bits 0–4 include any remaining active work, since
it belongs to the preceding link. At encryption start they indicate a different
epoch. Zero is evidence about these queues, not a proof against arbitrary RAM
corruption or a wrong host-supplied bond.

`connect_gap` uses the preceding Disconnection Complete service timestamp;
`prev_disconnect` is its age at failure. `r7_close_time_ms` additionally reports
the logical close-to-connect gap and close age. These distinguish the hardware
stop delay from the 131-ms host-observed interval. ENC_REQ and START_ENC_REQ ACK
times are service timestamps, not claimed RF timestamps. FFFFFFFF means absent.
Unsigned millisecond subtraction handles uptime/tick wraps for ordinary link
gaps; intervals of 2^32 ms or more cannot be distinguished.

No reliability fix was made without a proved cause. Fixes within the diagnostic
implementation preserve counter/publication ordering, cleanup scheduling and
failure-to-stop progress; they are not claims about an R4 defect.

## Reading and classifying a frozen record

Run `python3 tools/fm1_remote.py --port 'EXACT USB MIDI PORT' log` on the owner
machine. Save the complete output, including `r7_end` and the enclosing log's
end frame. `OWNER-BENCH-R7.md` lists the exact prefixes. Read `complete=1` only.
The first RX/protocol record and first TX mismatch are independent; match their
epoch to the journals. The R6 radio ring may have frozen on an earlier 0x08.

All R7 numeric fields are **hexadecimal**. Results are 0 fail/mismatch, 1
pass/match, 2 unavailable. `near_pass` is a bitmask, qualified by `near_valid`:
bits 0..4 mean C−2,C−1,C,C+1,C+2 in `dir`; bits 5..9 mean the same counters in
the opposite direction. RX dir is 1; TX dir is 0. A bit absent from `near_valid`
was not tried. Multiple authenticating candidates are retained, not silently
reduced to one. A 32-bit MIC is evidence with finite collision probability.

Apply the following overlapping rules together; `*` means any result. This
covers the four hardware/software combinations as well as the context checks.

| Evidence | Candidate cause / interpretation | What it does not establish |
|---|---|---|
| Any valid DMA/input equality is 0, or `input_stable=0` | Local DMA/working-buffer instability or wrong snapshot association. Compare both raw reads, headers/status and input-before bytes. | Which bus master changed it; a host Reset during checks can also invalidate ownership. |
| `key unchanged=0` | Key in use changed since derivation: local key-state corruption. | That all other state remained correct. |
| `key recomputed=0`, unchanged=1 | Key installed at derivation differs from software AES of retained inputs: derivation/AES fault, or corrupted retained LTK/SKD/comparator. | Which retained secret was wrong; no secret is printed. |
| `iv_unchanged=0` | Local IV differs from the captured clear exchange. | Whether a second concurrent fault affected the ciphertext. |
| `near_pass` has a non-C original-direction bit | Ciphertext authenticates at that counter offset; counter/sequence state differs from the attempted state. | Whether the central used the wrong count or the FM-1 omitted/repeated an RX. R6 descriptor context helps decide. |
| Only opposite-direction candidates authenticate | Direction/nonce-state mismatch. | Which side originally applied the wrong direction. |
| `previous key=1` | The packet authenticates under the most recent previous-link key and current IV. | Central stale key versus replayed/wrongly associated old bytes without supporting context. |
| `previous context=1` | Previous-link key and previous IV authenticate at C. | Whether the actual stale item was a PDU or session context. |
| hw_retry=1, sw_same=1; stable buffers/context | Original rejection was local/transient in hardware CCM or its inputs/readout. | Which AES transaction failed. KAT and CON snapshots narrow it. |
| hw_retry=0, sw_same=1; stable buffers/context | Reproducible hardware CCM/AES-path disagreement with independent software. | A passing KAT does not exclude a key-, block- or CCM-specific hardware problem. |
| hw_retry=1, sw_same=0 | Hardware/software disagreement (possible hardware false authentication or a verifier/input fault). | Do not classify this as merely a central bad packet; check offline with the owner's LTK. |
| hw_retry=0, sw_same=0; no alternate authenticates; key/IV checks pass; KAT=1; buffers stable | Bad authenticated data for all tested states: central/on-air fault, earlier corruption before the first retained snapshot, or a wrong LTK supplied by the host remain. | All-negative checks alone cannot uniquely distinguish these. Verify offline using the independently known desktop LTK. |
| KAT=0, with any other results | Independent local hardware AES known-answer failure; multiple faults can coexist. | A later passing retry cannot erase the captured failure. |
| Key recomputation=2 or previous-key check=2 | Required secret inputs were wiped/unavailable, or no previous encrypted link existed. | This is not a mismatch or failure. The derivation-copy and IV checks still apply. |
| kind=3; CCM results=2 | Local encryption phase/boundary rejection, without a rejected encrypted PDU. | It is not evidence of a bad received MIC. |
| TX record complete, tx_mismatches>0 | Hardware output differs from independently computed ciphertext/tag; it was withheld from radio and the link was closed. | A retry can pass after a transient fault. `sw_same=1` is still possible if wrong plaintext was encoded with an otherwise valid tag. |
| No complete R7 record; peer reported 0x3D / local 0x08 | No local CCM rejection or detected TX hardware mismatch was captured. | A peer/air failure is not classified by a local RX-only record. Preserve desktop evidence. |

With the independently known LTK, concatenate displayed SKDm/SKDs and IVm/IVs
in their displayed octet order. The standard-order CCM session key is
`AES128(reverse(LTK_HCI), reverse(SKD))`; the IV is `IVm || IVs`. The nonce is
five least-significant-first counter octets with direction in bit 7 of octet 4,
followed by the eight IV octets. AAD is header low byte masked with 3; plaintext
length is header length minus four. The final four displayed payload octets
are the MIC. This follows r8 Part 2 §§8.2/8.4/8.6 and the bundled TinyCrypt
AES interface. Keep the LTK and computed session key private.

A single record provides positive discriminators for hardware, buffer, key,
IV, counter, direction and previous-session faults. It cannot mathematically
prove the origin of a CRC-valid bad payload that was already corrupt before
our first snapshot, or independently verify an already-wiped LTK. No claim
that every all-negative combination uniquely identifies a physical side is made.

## Validation and artifacts

Final artifact/size fields below are filled from the build logs and SHA-256
record. Both target configurations pass the source-only link, DMA/MMIO,
RAM-code, multiply/load-erratum and memory-budget gates. No gates were relaxed.

| Profile | App image | .data + .bss / 98,304 | Pool / 344,064 | FWSC |
|---|---:|---:|---:|---:|
| R7 diagnostics on | 207,392 | 34,644 | 154,412 | 610,136 |
| R7 diagnostics off | 186,704 | 33,760 | 99,436 | 610,136 |

Diagnostics-on package: `template/build/template-open-r7.fwsc`.
SHA-256: `b829afc4cd2d3980fa3e1a73c63393ce891a97fa04a54c89c447ea601599ffea` (also `SHA256-R7.txt`).
The off package and symbol/map records use `template-open-r7-diag-off.*`.
`DIAGNOSTICS-OFF-R7.txt` records symbol/string exclusion. The R7 source
and artifact fingerprints are in `r7-source-record.json`.

Validation logs:

* `TESTS-R7-ALL.log`: full inherited ASan/UBSan seam, MIDI, storage, S1/S2/S3/
  S3.1, diagnostics on/off, F-28/F-29, migrations/byte cuts, instant/Reset,
  R6 radio-journal/DMA tests, 10 M ping events and 10 M inherited stress events.
* `TESTS-R7-HOST-DIAG.log`: the R7 profile with real NimBLE, object storage,
  F-29, legacy/SC, RPA/Reset, both F-28 repeat-pairing modes, encrypted updates,
  resumable Reset, control/refill timing and resolving-list bounds.
* `TESTS-R7-DIAG-FINAL.log`: the final R7 radio simulation, independent central,
  10 M ping events plus inherited 10 M stress events, timeout retention, and
  22 isolated directed cases. All five requested fault types are introduced
  only in the harness. Additional cases cover counter boundaries, changed key
  memory versus faulty derivation, previous key/IV, persistent versus transient
  AES faults, TX suppression, phase rejection, format/secrecy, Reset/reconnect
  freeze retention, audio preemption, old queued commands, automatic-empty
  toggles and KAT fairness under TX load.
* `BUILD-POLICY-R7.log`: four build-policy tests.
* `BUILD-R7-ON.log`, `BUILD-R7-OFF.log`: target configurations.

The inherited TinyCrypt `aes_encrypt.c`-only UBSan shift-base exclusion remains;
no R7 controller/HAL/test sanitizer exclusion was added. The HAL RX/refill
functions are extracted verbatim for the register-bus simulation. Existing
unmodified eager-release profiles retain their original stress assertions;
R7 pinned RX naturally changes backlog pressure and accepted-packet totals.
No artificial spontaneous MIC rate is injected into the long models.

Final R7 ping counts match the inherited oracle run: at 15 ms, 5,000,000
events, 705,930 requests / 705,929 completed responses and 356,540 ATT writes;
at 30 ms, 5,000,000 events, 984,008 requests/responses and 335,279 writes. One
15-ms request remains in flight at the finite boundary. Neither run has a
spontaneous MIC failure.

The final R7 inherited stress processes 10,000,000 events and 1,513,033
authenticated encrypted RX PDUs, with 229,713 repeats/lost ACKs, 126,750 CRC
errors, 48,320 misses, 2,269 updates and zero spontaneous MIC, late-instant or
supervision failures. Pinned-slot refusal is 2,938,005 (the legacy software
backlog-pressure metric is zero); the refusal/both-slot totals include preceding model setup/
ping activity as in the inherited harness. The unmodified eager-release
profile's full log separately records 2,280,220 authenticated PDUs, 256,465
repeats/lost ACKs, 126,643 CRC errors, 48,073 misses and 2,333 updates, also zero
spontaneous failures. These are different ownership profiles, not equal-throughput
hardware measurements.

Reproduce without hardware:

```sh
python3 tests/run_open_ble_tests.py
python3 tests/open_ble_build_test.py
# Individual R7 profiles:
python3 tests/run_ble_s3_tests.py --s31 --open-store --reliability
python3 tests/run_ble_s3_tests.py --mic-stress --reliability
```

The aggregate runner now includes both R7 profiles. The inherited full-run log
was collected separately before adding those calls; the two R7 logs provide
the additional final-profile results. `TESTS-R7-SHORT-FINAL.log` reruns the
complete aggregate entry point on the final source with `BLE_MIC_EVENTS=10000`;
the full 10-million-event evidence is the separate logs above, not that smoke
run.

Build the on image with the requested command:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble
```

For the off configuration append `--ble-diag off` (equivalent to
`ble_diag = false` in the app configuration). Do not overwrite the saved R4–R7
packages when switching profiles. Hardware timing/audio acceptance and the
next physical occurrence are owner work; neither was performed here.
