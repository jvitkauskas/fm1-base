<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# Open BLE round 3

Implemented in `/root/fm1/fm1-base-open` on top of the owner's uncommitted R2
work (`3b1e677` / `f08c9c2`, BSP `62a01ca7affe8de73707e309b57e98f1734c8b2d`).
R2's `BENCH-R2.txt` was read first. All R3 changes remain uncommitted. No
flashing, device access/enumeration, commits or pushes were performed. BSP
and the controller's RF/encryption/instant machinery were not changed in R3.
The owner's overnight R2 soak result is still pending separately.

## Deliverables

- Final diagnostics-on firmware: `template/build/template-open.fwsc`,
  **610,136 bytes**, identity `FM-1_1100`, development revision `open-r3`.
- SHA256: **`42a5b5602d93dcc0cc69cd0d1bb22e732399ebe3905eaabea70822c6b7a12eec`**.
- Required build log: `out/open-ble/BUILD-R3.log`.
- Hash file and machine-readable footprint: `SHA256-R3.txt`, `r3-manifest.json`.
- Full design/first-connection/event-field documentation: `docs/OPEN-BLE.md`.
- Current owner list: `out/open-ble/OWNER-BENCH-R3.md`.
- Diagnostics-on package/bin/map/symbols are also archived in `r3-diag-on/`;
  diagnostics-off counterparts are in `r3-diag-off/`. The final diagnostics-on
  rebuild was verified byte-identical to its archived package.

Required build command, completed successfully:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble
```

## Service Changed and Apple pairing

`open_ble/ble_bonds.[ch]` now retain a database generation per identity in the
OUR_SEC record's formerly reserved payload bytes 68..71. Record/object format,
CRCs, security keys, two-peer capacity and existing CCCDs are preserved.
R1/R2 zero means unknown/stale; R3 uses generation 3. New bonds start current,
ordinary security rewrites retain their generation, and replacing a bond
creates a new current one. This is a schema/cache-repair generation, independent
of firmware revision strings. Future attribute schema/permission changes must
bump it; ordinary value/code changes should not.

`open_ble/ble_host.c` sends only to the connected stale bonded peer, after
encryption, CCCD restoration and completion of SMP. It uses a direct indication
with explicit range 0x0001..0xffff, once per connection, and marks that peer
current only on ATT Handle Value Confirmation (`BLE_HS_EDONE`). A new bond,
a current bond or an unsubscribed bond receives no indication. An unsubscribed
stale bond remains stale until its indication bit is enabled. Unconfirmed
changes recur on reconnect. No global `ble_svc_gatt_changed` call remains.

R2's global call also marked other, disconnected subscribers `value_changed`.
On reboot, NimBLE can replay that flag during encryption before the application
sets the built-in Service Changed range, whose initial value is zero. R3's
store-read wrapper suppresses only that old automatic replay bit for 0x2A05;
its real CCCD flags restore normally. Per-peer generation checks now own the
change and always supply the valid full range. This eliminates a concrete
range/timing hazard and cross-peer fanout. It also removes the indication
R2 injected into the new Apple bond immediately after pairing.

Review of the public Apple ADG §§58.9–58.12 and BLE-MIDI 1.0 §§3–6 supports
retaining encrypted MIDI values. Discovery remains public; the protected MIDI
read returns 0x05 before SMP and an empty value after encryption. The existing
public MIDI CCCD can be written before pairing or again afterward; it does not
itself trigger SMP in this NimBLE pin. Notifications wait for encryption.
No unsolicited Security Request was added. The >=5-second, SMP-idle,
15-ms/latency-0/6-s L2CAP request remains intact. First-connection and bonded
reconnect orders, including the optional stale-cache step, are documented.

Confirmed generations are dirty in RAM immediately and committed after
disconnect through the existing atomic A/B object store. Flash writes remain
forbidden with a live link. A hard power cut before that idle commit may repeat
a confirmed indication; a failed save stays pending. After successful commit,
a same-generation reconnect is quiet. This tradeoff preserves the link/flash
contract instead of writing flash during Apple setup.

Conventional Service Changed works with existing iOS/macOS/Android/BlueZ
subscribers and their old bonds. Database Hash/Robust Caching were not added:
old clients could not have negotiated them, adding a GATT characteristic would
shift existing MIDI/DIS handles, and this pin does not implement the complete
hash/change-awareness/Database Out Of Sync machinery. The version discipline
and client CCCD dependency are explicit, rather than claiming robust caching.

## BlueZ findings and handle stability

DIS was already appended in R2. On-air handles remain GAP 0x0001..0x0005,
GATT 0x0006..0x000d (Service Changed value/CCCD 0x0008/0x0009), MIDI
0x000e..0x0011 (value/CCCD 0x0010/0x0011), DIS 0x0012..0x001a.
These assignments are now asserted by the simulated clients. New services
must go after the existing ones; inserting inside GATT would shift MIDI.

R1's last-service MIDI group could be cached with end 0xffff. R2's MIDI ends
at 0x0011 because DIS follows it. A stale client needs to invalidate that
boundary and re-discover all pages, including the 128-bit MIDI UUID between
16-bit service pages. The full indication covers those handles and the old
GAP name. Public group/characteristic/descriptor discovery does not depend on
encryption. No service reordering, public-discovery permission problem or ATT
pagination defect was found in this pin. The simulated stale client finds all
four services before encryption and while the indication is outstanding,
refreshes the name and uses MIDI both ways without pairing again.

The exact cause of the owner's corrupted BlueZ cache remains unproved without
its central trace. The pending-bit/zero-range path is a source-supported
explanation fixed here, not a claimed reconstruction of that particular bench.
Recovery of the actual cache and the Apple first-pair UI behavior require the
owner R3 bench. A client that disabled/never enabled 0x2A05 cannot be forced to
receive an indication.

## Event log and tools

Diagnostics builds retain 32 timestamped connection events across disconnects,
with session IDs: connect/address type/resolution, HCI encryption start/refresh
completion, actual SMP request/result/method/association algorithm, MTU, actual
CCCD writes/value/result and restored flags, Service Changed sent/confirmed,
L2CAP submission/response and applied interval/latency/timeout, disconnect,
protected-read rejection and successful empty MIDI read. `fm1_remote.py ble`
and `log` expose the same chronological ring. Logging snapshots before emitting
its rows avoids host-yield races. The ring is RAM-only and contains no keys.

Small diagnostic-only hooks in the pinned NimBLE files are documented in
`third_party/nimble/FM1-CHANGES.md`; they do not change protocol permissions.
One detail caught during review: this pin emits subscription callbacks for
CCCD reads, so actual writes are logged at the ATT descriptor handler, including
rejected values, rather than mislabelling reads as writes. Diagnostics-off
builds compile out the ring and hooks.

`fm1_remote.py read_log` discards incomplete attempts and retries up to three
8-second reads with 250-ms pauses, retaining the end-frame requirement. The
connection bench saves all completed measurements to `measurements.json` from
a `finally` block and adds `status.json` with completion/error information,
including setup/reconnect/USB failures and KeyboardInterrupt. Partial failed
USB reads are not merged into a successful snapshot.

## Verification

- **PASS** `TESTS-R3.log`: `python3 tests/run_open_ble_tests.py` runs all BLE
  S1/S2/S3/S3.1, object-store, seam/HAL/timing/privacy/update regressions and
  diagnostics-off integration under ASan/UBSan, with leak checking. The
  preexisting TinyCrypt AES shift-base exemption remains; no new exemption
  was added. This includes 17 actual NimBLE/LL/CCM GATT/SMP client runs across
  legacy, SC, RPA, repeat pairing and release configurations.
- **PASS** new client cases: new/current bonds quiet; protected read → SMP →
  empty read → MIDI CCCD; stale bond exactly one outstanding indication;
  disconnect without confirmation retries; confirmed generation survives store
  reload and reconnect; stale R2 pending-bit migration; full public mixed-UUID
  rediscovery including MIDI/name/bidirectional traffic; disabled indication
  CCCD remains stale until opt-in; no global change to another bond; stable
  handle assertions. Event checks include SMP order/method, ring wrap/session
  ordering, actual CCCD writes and rejected values, and no false write on read.
- **PASS** object-store migration: exact v1 zero-reserved record layout retains
  keys, security rewrites preserve stale generation, current peers remain
  independent, failed save does not mark the durable baseline current, linked
  flush is refused, no-op confirmation avoids another erase, and replacement
  starts current. Existing 10,474 byte-cut seed/bond atomic-store cases pass.
- **PASS** offline USB/bench/soak fixtures: bounded retry, successful final
  attempt, exhaustion after three attempts, completed measurements retained on
  USB error with partial status, existing HCI/counter/soak scheduling tests.
  No MIDI ports, HCI sockets or devices were opened by tests.
- **PASS** `BUILD-R3.log`, `BUILD-R3-DIAG-OFF.log`, `BUILD-R3-NO-BLE.log`:
  target RAM-only flash code, multiply/load hazard, no soft double, source-only
  link, DMA/trace bounds, memory footprint and MMIO boundary checks.
- **PASS** `BUILD-POLICY-R3.log`: four tests covering peripheral selection,
  diagnostics/version flags, forbidden archive injection, and negative
  application-MMIO fixture. Python syntax compilation and scoped diff
  whitespace checks pass.

The wider non-BLE base suite was not repeated: R2 already records unrelated
fastmath/mirror sanitizer failures. No fix or passing claim is made for them.

## Footprint and hashes

- Diagnostics on: app **194,064 B**, static .data+.bss **34,564 B**, pool
  **99,996 B**, RAMTEXT code **3,112 B**, trace **20,480 B**.
- Diagnostics off: app **184,544 B**, static **33,756 B**, pool **99,228 B**,
  RAMTEXT code **3,112 B**, trace **0 B**. Package SHA256:
  `7dacac7bfe4f313ffb3d5f807cf8ae9703c19de8e69459ac3fc2feb603f42fb7`.
- No BLE: app **80,172 B**, static **28,320 B**, pool **69,384 B**, RAMTEXT
  **3,000 B**, unchanged from R2. Package SHA256:
  `8882b3b00493381c7c66fa25f1c2a0f0594d2a17f768d5a006965d7f40e54e47`.

The event ring is 768 B (32 × 24); the on/off pool difference is exactly that
ring. All sizes are linker/build observations, not hardware stack or timing
measurements. The installable package's fixed padding explains its unchanged
size. The final requested image has diagnostics enabled.

## Clean-room boundary and owner work

All implementation reads were scoped to the allowed fm1-base/Felucca/r7/public
sources. Excluded trees, closed BLE code, vendor binaries and IR were not
consulted. The build copied/hashed the three expressly authorized opaque SDK
packaging files only for packaging; no contents were inspected. Public Apple
and BLE-MIDI document URLs/hashes are in `r3-source-record.json`; Core and
NimBLE references and the design rationale are in `docs/OPEN-BLE.md`.

The owner list covers retained Mac/iPhone/Android/BlueZ bonds after upgrade
(one indication/confirmation each, then quiet, MIDI without re-pairing), Mac
first connect after Forget, the second never-paired iPhone, and `ble`/`log`
after each. It distinguishes an evicted bond under the existing two-peer
capacity from a retained-bond test. Actual first-connect success, BlueZ cache
repair, RF behavior and long-duration operation remain owner hardware results;
R2's successful hardware checks do not establish these R3 outcomes.
