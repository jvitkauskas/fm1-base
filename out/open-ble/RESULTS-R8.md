# R8 — connected bond durability release candidate

Base: fm1-base `open-ble` at `6af1ded`. Source changes are uncommitted, including
`bsp/chip/fm1_flash.h` inside the BSP submodule. No device was contacted or
flashed. The owner's R7 bench identifies the desktop adapter's wrong MIC;
no encryption/MIC workaround was made. R7 reliability diagnostics and their
on/off selection are retained.

## Flash cost and why a masked connected save is unsafe

Sources are frozen `/root/wl82-ble-spec-snapshots/r8/` and the permitted open
source trees. References below are to sections in that snapshot, not to any
reconstruction or trace material.

* `04-platform.md` §0.9 specifies the existing XIP-off, IRQ-off BSP windows:
  a 4 KiB erase has a 500 ms wait bound, and a <=256-byte page program a 10 ms
  wait bound. **These are software bounds, not measured chip timings or a
  flash datasheet guarantee.** Real erase/program times remain unknown in
  r8; `00-overview.md` PB2.4 explicitly leaves that measurement open.
* `04-platform.md` §8.1 requires safe handling of issued-but-still-busy writes:
  poll from RAM to 2 s total for erase / 50 ms total for program; return
  LATE_READY only once ready, otherwise reset from RAM. Never restore XIP
  while the part still reports busy. The existing open BSP implements this;
  R8 retains the normal/extended bounds and failure classes.
* The actual FMOB v2 payload is 1,236 bytes. `device/storage.c:st_save` erases
  the inactive 4 KiB copy, writes five payload pages (256,256,256,256,212
  bytes), then programs the 32-byte A/B header last. Thus one save needs one
  erase plus **six** programs. The sum of normal command/wait budgets is
  560 ms, plus enter/exit, CRC, readback and scheduling overhead. At the
  extended limits the sum is 2.3 s before per-operation reset boundaries.
  The source does not establish a smaller physical maximum.
* With IRQs off, a 500 ms erase spans 66–67 event-service opportunities at
  7.5 ms and 33–34 at 15 ms, depending on phase. A 10 ms program spans 1–2
  at 7.5 ms and 0–1 at 15 ms. A lumped 560 ms blackout spans 74–75 / 37–38.
  These are unserviced event opportunities, **not measured RF misses**.
  The minimum legal supervision timeout is 100 ms (`03-link-layer-hci.md`
  §6.2); a freshly refreshed deadline can already expire during erase.
  Even a 6 s supervision timeout cannot protect pending LL instants or audio.
* The template renders every 256/44100 s = 5.805 ms. A 10 ms program already
  exceeds that audio deadline. Erase used `audio_silence()` to avoid looping
  stale samples, which creates an audible interruption even if a late-half
  counter does not detect every skipped half. That is unsuitable for this fix.

What the baseband can do without CPU service is described by `02-baseband.md`
§1, §§4.2–4.3 and §5.8: programmed anchors/windows, channel hopping, IFS,
CRC/whitening, SN/NESN and retransmission/acknowledgement of owned slots,
and automatic empty transmission continue in hardware. It cannot release
full RX slots, refill TX, perform CCM/control processing, program a pending
instant, or run software missed-event widening/supervision on its own. A/B
atomicity does not make a radio blackout safe.

The earlier `worker_gap ... reason radio_start,flash_write` near 17.9 ms is
an **inter-worker dispatch gap** with overlapping reason scopes. It is not
an erase timing measurement or a connected-write upper bound. The reason
annotation may span a disconnect, idle save, and advertising restart: an
endpoint of `link_up` does not prove flash was issued with a link up. This
is already explained in `RESULTS-R2.md` and implemented by `ble_health`.
The old idle guard was checked at every physical issue. R8 adds separate
physical-operation and live-save counters instead of inferring cost from
that historical gap.

A write just after an event cannot bound a 500 ms blackout below supervision
or the 5.8 ms audio deadline. Peripheral latency does not make audio code or
constants accessible during XIP removal. Splitting erase from program still
leaves the erase blackout. r8 supplies no qualified NOR suspend/resume
contract, so no such commands or assumed chip timing were introduced.

## Chosen mechanism: keep the CPU and interrupts live in SRAM

r8 `04-platform.md` §0.9 explicitly identifies moving handlers/data to RAM as
the alternative to accepting link loss. R8 goes further: **all runtime code
and constants**, including app/audio, NimBLE, controller, crypto, interrupt
wrappers and lookup tables, execute from SRAM. No subset call graph or cache
residency assumption is needed. Only `_start` and the tiny `fm1_cstart`
bootstrap execute from XIP. The bootstrap performs volatile word copies,
then calls the SRAM runtime through a pointer; C startup does not recopy
its executing image. The existing PC guard uses `_rt_start.._rt_end`.

`device/app_open_ram.ld` reserves the original stacks/vector/NOINIT addresses
and the boot probe at `0x01C07F00`. Runtime SRAM is
`0x01C08000..0x01C3BFFF` (208 KiB), static RAM
`0x01C3C000..0x01C45FFF` (40 KiB), and the pool
`0x01C46000..0x01C73FFF` (188,416 bytes). The 20 KiB R7 trace occupies the
separate bottom SRAM region and is absent in the release profile. DMA stays
in the ordinary SRAM class required by `04-platform.md` §§0.2–0.3; no cache
RAM or ROM is used. The build enforces region sizes, the 8 KiB pool reserve,
SRAM runtime text/data, no calls to XIP, source-only linkage and MMIO ownership.
Fault-injection build-policy tests reject both an XIP function and an XIP
constant, as well as a direct runtime call into XIP.

The RAM flash driver restores the caller's interrupt state **before** erase
or program. Runtime callers therefore leave BLE RX/event, LL executor,
audio and TIMER5 enabled during command transfer, busy polling, cache
invalidation and SFC restoration. Their priorities and normal bounded work
remain unchanged. No flash wait or `audio_silence()` occurs with the runtime
audio IRQ suppressed. SPI0/SFC have one main-context owner; none of these
ISRs uses them. A busy NOR is never read through XIP: mapped platform reads
are refused while `fl_busy` is set, and XIP is restored only after ready.

While waiting, with CS high, the main-context flash loop services
`ble_host_worker()` and `app_input_only()` every 500 us of polling. This keeps
host MIDI and keyboard input moving as well as servicing the radio in IRQs.
The spacing is a polling budget, not a target measurement: SPI command time,
callbacks and existing IRQ work must be included in the physical bench.
The host can change bonds/CCCDs during this service, but cannot start another
physical save: the bond flush has an in-flight guard and the generic object
store refuses reentry. The verified baseline is the **original snapshot**,
not whatever live bytes exist when the wait ends. Changes made during the
write remain pending for a subsequent deadline.

Only the OBJ_BLE adapter opens the live physical-write gate. Other objects
retain their advertising-pause / no-linked-save contract; the boot seed
still requires radio idle. Calibration stays read-only. No disconnect,
latency negotiation, extra flash sector or on-flash format change is needed.
The A/B writer only erases the inactive copy, commits its header
last, verifies that copy, and then the FMOB adapter verifies the whole object
again before replacing its RAM baseline. A failure keeps the old baseline
and the pending state. The other A/B copy and loader-marker tails are intact. Read I/O failures are now distinguished
from invalid/blank copies: a save refuses before erase if it cannot determine
the current copy, instead of treating an unreadable store as empty.

## Durability deadlines and limits

Security creation, key changes and deletions use a fixed **250 ms** deadline
from the first such change. A lower-priority pending save is promoted.
Automatic flush waits until `ble_sm_num_procs()==0`, so it does not persist
half-complete key distribution. Eligibility is therefore the later of
`first security change + 250 ms` and completion of SMP, plus one host pass.
Normal durable completion adds the transaction's elapsed time (the command/
wait budgets sum to 560 ms, with the overhead above). This is independent of
a central keeping the connection alive indefinitely.

CCCDs, GATT cache generation and recency changes use a fixed **5 s** deadline;
repeated writes cannot postpone it. They can piggyback on a security save.
Connected failures retain pending state and back off for one second before
the next automatic attempt. Idle saves retain the existing immediate-flush
behavior. After a successful in-flight snapshot, a new change gets its own
250 ms / 5 s deadline. Net-state no-ops still avoid physical writes.

A power cut before a committed/read-back save can lose the new bond; the
bounded pending window is not zero. A cut while writing leaves the prior
complete object or the complete new one, never a partially loaded key/CCCD.
Persistent hardware write failures cannot be turned into durable state;
`bond_failures`/`bond_pending` expose that condition.

This SRAM profile defaults on for `ble = "open"`. Oversized apps fail the
link. `ble_connected_store = false` explicitly selects the original XIP
layout and safe idle-only behavior; it retains the unplug-while-connected
limitation. An app whose audio/input handlers directly access mapped external
flash (for example external flash samples) needs another integration; moving
its compiled code/constants alone does not make those reads safe. The
provided template uses SRAM data throughout the service path.

Felucca has the same idle-only flaw, but the complete SRAM mechanism does
not fit. Its existing S3.1c target record reports image 527,036 bytes, static
RAM 97,064, pool 331,108 and only 1,132 bytes spare beside RAM code/trace
(`/root/fm1/Felucca-ble/out/ble-s3.1c-build.log`). Its live-write gate and
original journal remain unchanged. Its idle host flush now preserves the
pending retry flag after an unsuccessful write, and its documentation states
the remaining connected limitation. No unsafe relocation subset was guessed.

## Counters and owner acceptance

Both profiles expose these via the existing USB `ble` / `log` readout:

* `bond_pending`, `bond_writes`, `bond_failures`: RAM state differs from the
  verified baseline, successful whole-object saves, failed save attempts.
* `bond_connected_writes`: successful platform A/B writes issued with a link
  up. Require it and `bond_writes` to increase while the same epoch remains
  connected, with `bond_pending 0`. The latter is the FMOB readback authority.
* `bond_save_us_last`, `bond_save_us_max`: elapsed main-context live save
  duration through A/B verification, including serviced IRQs and callbacks.
  This is busy time, not a CPU/IRQ blackout.
* `bond_erase_us_max`, `bond_program_us_max`: longest issued-operation duration
  returned by the RAM driver during live saves, including time spent servicing
  callbacks/IRQs before ready is observed. Read these on hardware; no fabricated
  typical flash duration is supplied.
* `bond_stall_us_max`: longest measured short IRQ-off admission-guard span
  (rounded up from 24 MHz ticks). The flash command/wait itself is preemptible;
  no flash-busy blackout is designed. Compare the ordinary executor/refill/
  IRQ counters too; this counter does not replace all runtime lock telemetry.
* `bond_missed_events`: cumulative delta of serviced connection events with no
  RX while live writes ran. Background RF loss can also increment it, so it
  does not assign causation to flash. It is not guessed from erase duration.
* `bond_sup_margin_us_min`: least sampled supervision time remaining during
  the **last live attempt**, sampled at worker/event IRQs and entry/exit;
  zero before any live attempt carries no measurement.
* `bond_sup_risk`: sticky flag if an observed live-write margin fell below two
  current connection intervals (or the deadline was already reached).
* `bond_write_link_losses`: live saves whose link ended or epoch changed.

Counters/maxima and sticky risk reset on boot, not on an ordinary reconnect.
The last-attempt supervision minimum is restarted at each live save. They
remain available with `--ble-diag off`; the detailed R7 journal is excluded.
Require `audio_late 0`, `audio_xruns 0`, no new link losses/risk/misses in good
RF, and no audible/MIDI interruption. See [OWNER-BENCH-R8](OWNER-BENCH-R8.md).

## Validation and candidates

The implementer ran no hardware bench. Target flash durations, SRAM execution
under maximum audio/RF/USB load, stack peaks with the main-context service,
and DMA at the new pool addresses remain owner qualification items. The
software construction removes flash-caused IRQ blocking; a host model cannot
prove physical audio timing, RF continuity, or behavior of a failing NOR.

The new ASan/UBSan tests exercise the actual A/B storage and FMOB/bond code:
1,001 pairing/power-loss times; live SMP completion gating; fixed CCCD and
promoted security deadlines; snapshot mutation during erase; failure/retry;
read-error refusal without erasing either copy; seed refusal while linked; original XIP-profile refusal; and every byte cut
through erase/payload/header in both write directions with the current copy
byte-exact. Original v1 migration, ninth-peer/F-29 snapshot cuts and the actual
R4 downgrade parser remain in the suite. Real NimBLE legacy/SC/RPA and F-28
profiles now also persist and reload bonds/CCCDs before closing their encrypted
connection, then reconnect using the durable LTK.

The flash timing harness extracts the production RAM wait verbatim. At both
7.5/15 ms and 100 ms/6 s supervision it services 74/37 connection events and
96 audio blocks during 560 ms of write budgets, with no flash-blocked event
or model audio xrun. It exercises late-ready and unresolved-busy reset with
interrupts still live. A counterexample using the old IRQ blackout expires
100 ms supervision. Separate production-platform seam tests cover both
connected/idle profiles, IRQ state, host/key servicing and the cost/miss/risk/
link-loss counters. These are software-model results, not device timings.

The full BLE suites include F-28/F-29, diagnostics on/off, the R7 host/radio
profiles and their full 10-million-event ping and inherited stress models.
Evidence logs:

* `TESTS-R8-ALL.log`: complete inherited aggregate, ASan/UBSan, F-28/F-29,
  both diagnostic selections and R7 profiles; full 10 M ping plus 10 M stress
  events for both original and R7 slot-ownership profiles. No spontaneous
  MIC/late-instant/supervision failures. The original stress authenticated
  2,280,220 PDUs; R7 authenticated 1,513,033, with 229,713 repeats/lost ACKs,
  126,750 CRC errors, 48,320 deliberate misses and 2,269 updates. R7 pinned
  RX slot refusal is 2,938,005. Ping totals match R7: at 15 ms 705,930
  requests / 705,929 replies (one in flight at the finite boundary); at
  30 ms 984,008 requests/replies. Each interval runs 5 M events.
* `TESTS-R8-FINAL.log`: final aggregate after the storage I/O hardening and
  new production flash/platform seam tests; `BLE_MIC_EVENTS=10000` for this
  additional complete regression. Its short stress count is not substituted
  for the full 10 M evidence above. Both diagnostic modes and the R7 host
  profile include the final connected-write interruption/read-fault tests.
* `TESTS-R8-HOST-LIVE.log`: real NimBLE integration, F-28/F-29, whole-object
  cuts/migration and live bond/CCCD reload before disconnect.
* `TESTS-R8-BASE.log`: all platform, fast-math, backup, mirror/viewer, host
  template and actual R8 release-package OTA/update-loader tests with
  ASan/UBSan and leak detection. No update-path test is skipped.
* `BUILD-POLICY-R8.log`: five build-policy tests, including XIP runtime
  function/constant/call rejection. `BUILD-R8-ON.log` and
  `BUILD-R8-RELEASE.log`: final target builds.
* Felucca `out/TESTS-R8-S1.log`, `TESTS-R8-S2.log`, `TESTS-R8-S3.log`,
  `TESTS-R8-S31.log`, `TESTS-R8-MIC-10M.log`: its inherited BLE sanitizer
  suites including F-28 and full 10 M ping / 10 M stress events. Stress
  authenticates 2,280,220 PDUs with zero spontaneous MIC failures.

The full-event log was captured before the final store read-error refusal;
that change affects the A/B path, which those radio models do not compile.
The final aggregate and platform/update tests exercise the final store source.

Platform ASan/UBSan exposed two pre-existing issues: `fm_exp2_nf` shifted a
negative signed exponent, and the mirror encoder speculated a second row
after its first row already exceeded the nominal frame cap. The fixes use
unsigned exponent bits and stop that second speculative row. Existing
fast-math/audio and mirror/viewer tests validate the changes, without a
sanitizer exemption. The loader test also frees its two package buffers so
LeakSanitizer can finish the update-path test cleanly. The inherited TinyCrypt AES-only shift-base exemption
in the BLE runner remains unchanged.

Build commands:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble --package-name template-open-r8.fwsc
# append --ble-diag off and use --package-name template-open-r8-release.fwsc
```

Diagnostics candidate: image 208,828 bytes; SRAM runtime 208,252; static RAM
35,192/40,960; pool 154,428/188,416; trace 20,480. Release candidate: image
188,172; SRAM runtime 187,596; static RAM 34,308/40,960; pool 99,452/188,416;
trace zero. Both packages are 610,136 bytes; loader 6,980. SRAM figures are
separate occupied regions; runtime code/constants are additional to static
RAM and the pool. Both builds pass the reserve/layout and source/MMIO checks.

Package hashes are in [SHA256-R8.txt](SHA256-R8.txt):

```text
c5422315d7111ad67932b1ef31d910fa82766561f9a9f94b6fbbb471a68e95e3  template/build/template-open-r8.fwsc
75dea09ae763aff0a828f2e4e4a5195618e99be1c8cf6fffda6cab9017df7180  template/build/template-open-r8-release.fwsc
```

`PRIOR-PACKAGES-R8.txt` records the preserved R4–R7 packages. Matching `.bin`,
`.elf`, `.map`, `.symbols` and `.dis` files are saved for each R8 profile.
`DIAGNOSTICS-OFF-R8.txt` verifies release string/symbol exclusion and retention
of persistence counters. The upgrade/downgrade note is in the README and
`docs/OPEN-BLE.md`: R4→R5+ migrates (owner-verified); R5+→R4 rejects the truncated
v2 object and starts with no bonds, so the central must forget and pair again.

Source fingerprints and artifact sizes/hashes are in `r8-source-record.json`.
`R8-bsp.patch` preserves the required dirty submodule change for review;
`R8-felucca.patch` records Felucca's limited retry/documentation change.
No commit, flash, upload or device-contact operation was performed.
