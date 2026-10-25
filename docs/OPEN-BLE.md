<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# Open BLE-MIDI for fm1-base

This is the clean-room WL82/AC79 peripheral controller, implemented from the
frozen **r8** specification. R8 adds bond persistence during a live connection
in fm1-base using a complete SRAM runtime. Diagnostics remain the R7 profile;
the desktop adapter's confirmed wrong-MIC transmissions require no controller
change. R8 is a source-tested release candidate awaiting the owner's physical
bench. See [RESULTS-R8](../out/open-ble/RESULTS-R8.md) and the short
[OWNER-BENCH-R8](../out/open-ble/OWNER-BENCH-R8.md). Earlier sections below are
historical round records.

## Upgrading and downgrading

**R4 to R5 or later:** the old v1 bond object migrates to v2. This upgrade was
verified on hardware. Keep the existing pairing on the Mac/iPhone.

**R5 or later to R4:** R4 truncates a v2 object to its old 1,108-byte load size,
then rejects it because the CRC fails. R4 starts with no bonds. The central
still has its keys, so forget the FM-1 on that central and pair again. A later
R4 save can replace the newer object; do not expect a round trip to recover
those bonds. This downgrade behavior is intentional and covered by the host
test using the original R4 parser.

## Connected persistence and SRAM budget (R8)

Open BLE now defaults to `ble_connected_store = true`. The linker copies all
runtime code and constants, including NimBLE, the app, every interrupt handler,
and audio data, to SRAM before C startup. Only the entry/bootstrap runs from
XIP. The build checks the layout and rejects XIP runtime text/data or calls;
apps which exceed the SRAM regions fail the build. No vendor code is added.

A security change becomes eligible after 250 ms and completion of SMP key
distribution. CCCD/cache/recency changes use a fixed five-second deadline;
ongoing CCCD traffic cannot postpone it. Flash busy polling leaves interrupts
enabled and services the host and `app_input_only` every 500 us of waiting.
These callbacks run in the main context with recursive physical storage
excluded. Object reads of busy flash return an error. App input/audio code
must use SRAM data and must not access mapped external flash directly; an
app using external flash samples must provide a different storage integration.
Normal application code should use the platform storage APIs.

The atomic A/B `OBJ_BLE` format is unchanged. Only a verified readback advances
the RAM baseline; a failed save stays pending and retries after one second.
A power loss before commit can still lose the new bond: wait for
`bond_pending 0`, `bond_writes` increasing, and `bond_connected_writes` increasing
while `peripheral_linked 1`. Both profiles expose the new flash/link counters.

For an app too large for this SRAM layout, `ble_connected_store = false`
selects the old XIP layout and safe idle-only writes. That fallback retains
the original unplug-while-connected limitation. Felucca cannot fit the full
SRAM runtime and retains that limitation too; its retry flag is fixed.

## Build and application interface

For a firmware's fm1.toml:

```toml
ble = "open"
ble_central = false
ble_diag = true             # default for now; false removes detailed diagnostics
```

Or select the open variant of the unchanged template for one build:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
python3 tools/build.py template --open-ble
```

This writes `template/build/template-open.fwsc`; it overrides `ble` and turns
central off for that invocation. `ble = "open"` with `ble_central = true` is a
clear configuration error. Central support needs a future specification
extension. The template's default stays `ble = true`, `ble_central = true`:
its original JieLi stack selection is unchanged. `--no-ble` builds
`template-usb.fwsc` and excludes both stacks. No closed-stack build was run.

Apps keep `TO_BLE`, `PORT_BLE` and the peripheral plat.h API. They supply
`OBJ_BLE` and a nonoverlapping 8 KiB A/B pair in `APP_STORE_MAP`, as before.
The template already uses `0xDE000`/`0xDF000`; no new app object is required.
The open format is incompatible with the closed stack's OBJ_BLE contents;
switching stacks does not migrate pairing, and saving in either format
replaces the other format.

The profile is one peripheral BLE-MIDI link, ATT MTU up to 517 (attribute values up to 512 bytes), legacy and
Secure Connections Just Works pairing, eight peer bonds with durable LRU, peer-RPA resolution,
encrypted reconnects, and central-initiated map/connection updates. There is
no central/scanner role or WiFi. Advertising name: **FM-1 Open BLE**.

`BLE_OFF`, `BLE_STARTING`, `BLE_READY` and `BLE_FAILED` describe lifecycle.
READY means the host has synchronized; `plat_ble_linked()` means a connection
exists and `plat_ble_connected()` additionally requires MIDI notification
subscription. `plat_ble_enable(0)` is applied in main, stops advertising and
requests link termination, with retry while a link remains. On resumes
advertising without reinitializing NimBLE. Calls from app/tick contexts only
publish the desired state. Main and LCD yield points service host work with
a reentrancy guard; no host callbacks or allocation run in the executor.

At boot, the seed is advanced before audio/USB regardless of APP_BLE_AT_BOOT.
After platform setup, APP_BLE_AT_BOOT controls the normal start. A retained
active-run marker plus a new fault or watchdog reset inhibits automatic
Bluetooth restart; manual `plat_ble_start()` still starts it. Clean updater /
UBOOT exits clear that marker. A cold power-on is not treated as a crash.
The 48 x 43-character log ring and breadcrumbs survive warm resets in
`.noinit`; power-on contents are validated by magic, not assumed preserved.
A start resets the ring's line count. Fatal reporting does not re-enter the
host.

BLE input/output use the existing four-byte USB-MIDI event representation,
including SysEx and real-time messages. TX holds 128 USB-MIDI packets; RX holds 512. The RX ring holds the worst-case
expansion of one maximum-size MIDI value (509 running-status program changes).
The host retains queued notifications on temporary allocation/backpressure.
The platform's actual MIDI functions update PORT_BLE counts and activity,
excluding real-time messages. A subscribed full BLE queue contributes **zero**
to the minimum returned by `plat_midi_out_room(TO_USB | TO_BLE)`; an absent
listener is ignored. Producers must honor that result. MIDI is carried into
the template synth and screen; the template's keys transmit to USB and BLE.

## Module map

| Module | Responsibility |
| --- | --- |
| `open_ble/ble_logic.c`, `ble_controller.c` | Calibration record decoding, in-memory HCI, command queues, redacted trace |
| `ble_link*.c`, `ble_security.inc`, `ble_privacy.c`, `ble_timing.c` | Peripheral LL, encryption, instants, resolving list, resumable work/budgets |
| `ble_health.c` | Lifetime gap records and advertising-session acceptance counts |
| `ble_crypto.c`, `ble_seed.c` | AES/CCM glue, hash generator, boot seed/readiness |
| `ble_bonds.c`, `ble_store.c` | Canonical identity-keyed cache, net-state comparison, atomic object snapshot |
| `ble_host.c`, `ble_midi.c`, `ble_npl.c`, `ble_heap.c`, `port/` | NimBLE application/NPL, MIDI codec, bounded heap/config/libc declarations |
| `open_ble/hal/fm1_ble*.h` | Radio/baseband, DMA, TIMER0, nesting-safe locks, AES and read-only calibration access |
| `device/ble_open*.c` | fm1-base seam, executor/host hooks, lifecycle, log output |
| `bsp/chip/fm1_isr.S`, `fm1_flash.h` | Optional ISR accounting, safe flash completion, idle/calibration write guards, plain reads |
| `device/unity.c`, `main_fm1.c`, `plat_fm1.c`, `app.ld` | Boot phases, poll/yield, storage/MIDI integration, explicit memory sections |
| `third_party/nimble/`, `tools/nimble_build.py` | Pinned Apache NimBLE 1.9.0 and bundled TinyCrypt source list |

`open_ble/hal/` is a narrowly scoped BSP extension for the undocumented radio
peripherals and dedicated executor. The register checker explicitly documents
this boundary, checks all other open-stack software recursively, and retains
the checks on device/, include/, lib/ and app code. A negative fixture verifies
that application register access is still rejected. The unlinked closed
`device/radio.c` is omitted from open/no-radio audits; builds that compile it
retain the original audit.

The BSP is pinned to `jvitkauskas/felucca-bsp`, `open-ble`, `62a01ca`.
Round 2 changes no BSP files. Existing open-only ISR/flash adaptations stay
optional behind `FM1B_BLE_OPEN`.

## Platform and interrupts (r7 04 §§0.6, 0.8)

Higher numeric levels preempt lower ones; the template uses CPU 0 only.

| Level | Line | Handler / work |
| --- | --- | --- |
| 7 | 1 | Fatal CPU exception; does not return |
| 6 | — | Unused |
| 5 | 29, 45 | BLE RX and event-close; above every returning long handler |
| 4 | 63 / TIMER5 | 10 kHz panel scan, 2 kHz USB/TRS, app's optional 1 kHz tick |
| 3 | 11 / ALNK0 | Audio render and USB audio mixing/tap |
| 2 | 4 / TIMER0 | LL executor, below audio and scan/USB |
| Main | — | NimBLE, SMP/ECC, object commits, app UI |

P2 runs after JEDEC/unencrypted-window setup and before LCD/audio/USB. It reads
calibration, loads OBJ_BLE and commits the next seed. P3 attaches all three
controller vectors masked, after audio setup and before the vector write lock.
P4 performs radio bring-up only when Bluetooth is requested. With the BSP
interrupt gate enabled, the TIMER0 self-test requests exactly one entry within
1 ms before NimBLE can open an advertising instance. A failed probe/self-test
fails startup. Baseband lines unmask only when an instance opens.

TIMER4 supplies continuous 24 MHz ticks; the BSP's TIMER5-maintained fm1_ms is
the monotonic host clock. TIMER0 uses a 1 us requested expiry and 1 ms autonomous
backstop; it is never shared with an app. Pending requests coalesce and a
request during a pass causes a subsequent entry. The nesting-safe ICFG save /
restore preserves the interrupted state, including in flash and app locks.

Every returning handler above the executor is wrapped with nested preemption
accounting. The executor subtracts that time once. Limits retain r7 values:
dispatch <=7,500 us without negative samples, own work stops before the next
item after 250 us, completed own pass <=250 us + longest item, item <=100 us,
refill <=80 us. Per-kind counts/maxima/overruns and max opcode are exposed.
The self-test proves dispatch wiring only; none of the physical timing bounds
is established for this port by a native test.

DMA is explicitly NOLOAD inside the pool's verified SRAM class, with linker
assertions and target build checks. With diagnostics enabled, the 20,480-byte trace is separately NOLOAD
in RAMTEXT; the trace writer initializes each used record. It is absent with
`ble_diag = false`. The host ring is inside cleared BSS. NimBLE
static pools and software buffers are counted in the pool; stack ranges and
hardware guards retain the BSP layout. The radio's one-time calibration silences audio via
radio_quiet before the executor starts; the executor stays below audio afterward. The USB log includes
audio late/xrun/wall maxima. New stack margin, actual IRQ/flash blackout and
coexistence timing remain owner bench measurements.

The only TinyCrypt source change calls a pure out-of-line 32x32->64 multiply
on this target, following `bsp/docs/CPU.md`. Original notices are intact; the
final target disassembly reports no multiply inlined beside a load.

## Round-2 gaps and advertising restart

`worker_timer_over_7500us` and `worker_gap_max` are lifetime observations,
including while off, and survive HCI counter resets. Each `worker_gap` row
records the end time in milliseconds since boot, the exact gap in ticks
(internally; microseconds in the USB log), state at the preceding IRQ, end
state, reason bits, and link epoch at the returning IRQ. `link_up` takes
precedence if either endpoint has a link, including creation/closing. The ring
keeps the last four gaps strictly greater than 7,500 us; the maximum has its
own record even after the ring wraps. `unknown` remains explicit. Reasons
identify overlapping operations, not proof of a causal stall. Nested
`boot`, `radio_start`, `radio_stop`, and `flash_write` scopes survive until the
next IRQ even when that IRQ runs after the scope ends. TIMER4 differences
extend the timestamp across tick wrap and interrupt-masked flash operations.

Calibration is called only once, before TIMER0 starts. Off/on keeps the
initialized radio and NimBLE; it cannot enter calibration with a live link.
Both platform and HAL reject startup if the instance/link is active. Restart
preparation is resumable: at most 256 bytes of DMA clearing or 64 bytes of
frame publication per executor item. Stop polling is nonblocking, with a
20–100 ms wall-time bound before quarantine; that bound is not an IRQ blackout.
There is **no intentional >7.5 ms timer blackout on re-enable**. R1's 17,804 us
sample cannot be attributed retrospectively. An idle flash erase/program is
a plausible source: flash intentionally excludes interrupts while XIP is
unavailable and can exceed 7.5 ms. The existing RAM flash guard forbids every
issued write while linked. Physical operation durations remain owner tests.

Boot, re-enable, disconnect and save-resume all use the same public identity,
ADV_IND type, three-channel map, open filter, 20 ms interval, primary MIDI
service data and scan-response name. Before the instance is enabled, both TX
descriptors/payloads and RX slots are prepared; the RX acceptance state is
published under the IRQ lock before the hardware enable. A native test
injects CONNECT_IND at the very first unlock, before advertising-start returns.
There is no observed software window where published advertising cannot accept
a valid CONNECT_IND. Rapid on while termination is pending waits until the
link ends. No restart defect explaining the first R1 BlueZ failure was found;
its cause remains unproved and may be central-side.

`adv_session`, `connect_seen` and `connect_accepted` describe the current
advertising session. Seen counts CRC-valid CONNECT_IND PDUs presented to the
advertising RX decoder, including malformed or wrong-address requests;
accepted counts successful LL creations, before establishment. Diagnostics
also keep the last four `adv_session_record` rows. These are controller RX
observations, not a count of packets sent by the central or a capture of all
RF traffic. Session IDs, counters and gap history survive HCI Reset.

## Apple

Reviewed against Apple's [Accessory Design Guidelines, 2026-09-21](https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf)
(ADG, chapter 58 and §44), the MIDI Association's
[BLE-MIDI 1.0, §§3–6](https://midi.org/midi-over-bluetooth-low-energy-ble-midi)
(the PDF linked on that page), and [Core GAP §9.3.9 and Appendix A](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-62/out/en/host/generic-access-profile.html).
These are source checks; R2 Apple bench results are in `BENCH-R2.txt`,
and R3 hardware acceptance remains pending. No Apple certification is claimed.

- **Connection parameters:** the peripheral now requests exactly 15 ms
  (`12/12` HCI units), latency 0, timeout 6 s (`600` units), once per link.
  This satisfies BLE-MIDI §4's at-most-15-ms request and ADG §58.6's general
  15-ms exception, zero-latency bounds, timeout range, and timeout inequality.
  The old 7.5–15 ms / 4 s request fails the current general ADG rules.
  Older [QA1931](https://developer.apple.com/library/archive/qa/qa1931/index.html)
  specifies a different timeout range; the current ADG controls this choice.
  No HID service is claimed to obtain an 11.25-ms exception. A central may
  choose 30 ms; that is accepted, but is less suitable for live MIDI.
- **Request timing:** wait at least 5 s after Connection Complete (Core GAP
  `conn_pause_peripheral`), and wait for SMP to finish. Busy requests retry
  after 1 s; a completed/rejected request does not loop. With no LL Connection
  Parameters Request feature, NimBLE sends the L2CAP update request. Log
  `param_pending`, `param_requests`, `param_status` and actual negotiated
  parameters. The 7.5/11.25/15/30-ms central test matrix remains supported.
- **Advertising:** ADG §§58.2–58.5: connectable ADV_IND on 37/38/39, flags,
  primary MIDI UUID in ADV, and a complete local name in SCAN_RSP. The former
  100-ms interval is replaced by exactly 20 ms on every session. It remains
  20 ms indefinitely, meeting the initial-at-least-30-s recommendation at a
  power cost; the optional longer-interval phase is not used. TX Power Level
  AD remains omitted: calibrated dBm is unknown in r7 and must not be invented.
  This is an outstanding ADG §58.4 recommendation, not a full-compliance claim.
- **MTU:** ADG §58.11 and BLE-MIDI §6: NimBLE responds with local MTU 517;
  effective MTU is the minimum of the peer/local values. ATT's maximum
  attribute is 512 bytes. Both small MTUs and fragmented large values work;
  the native test receives a maximum MIDI value without RX overflow. This
  controller has no packet-length extension; no DLE procedure is falsely
  advertised. The empty MIDI read remains empty after any required security
  procedure (BLE-MIDI §5).
- **GAP name/appearance:** ADG §§58.4, 58.12.1: the initial readable and
  writable Device Name is now `FM-1 Open BLE`, matching advertising. The name
  has no colon/semicolon. A name written by a central is volatile and used in
  the next scan response (shortened AD type if it exceeds 29 bytes); power-on
  restores the default. Appearance stays unspecified (`0x0000`); this is not
  a HID accessory and ADG imposes no MIDI-specific appearance here.
- **Pairing/security:** ADG §§58.9–58.10 and BLE-MIDI §3: discovery of services,
  characteristics and descriptors is public. MIDI value reads/writes require
  encryption. An unpaired read returns Insufficient Authentication; after
  pairing the initial read returns no payload. The previous unsolicited
  100-ms Security Request is removed. The central initiates pairing, with
  NoInputNoOutput Just Works, bonding, 16-byte keys, SC and legacy support;
  no MITM interaction is promised. Notifications wait for encryption. Both
  sets of distributed keys and bonded CCCDs survive reconnect; removal
  deletes both directions.
- **GATT cache/firmware changes (R3):** Service Changed is driven by a stored
  per-bond database generation, not by encryption or a firmware version string.
  The implementation and migration are described below. New bonds never
  receive an indication on their first connection. Existing stale bonds
  receive one explicit full-range indication and become current on ATT
  confirmation. All GAP/GATT/MIDI handles retain their R1/R2 assignments.
- **Device Information:** ADG §58.12.3: a non-advertised DIS now exposes
  manufacturer `M-VAVE`, model `FM-1`, firmware revision and software revision.
  The manufacturer identifies the hardware, not endorsement of this firmware.

## R3 Service Changed and cache migration

This implements conventional Service Changed caching, following
[Core 6.2, Vol 3 Part G §§2.5.2, 3.3.3.3, 7.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-62/out/en/host/generic-attribute-profile--gatt-.html).
`BLE_GATT_DB_VERSION` in `open_ble/ble_bonds.h` is currently **3**. It identifies
an attribute schema/cache repair generation, independent of `APP_VERSION` and
DIS firmware/software text. Bump it whenever services, declarations, handles,
properties or permissions change in a way that invalidates a client's cache;
ordinary code fixes and readable value changes do not require a bump. This
profile registers a static database once, so a manually maintained generation
has a clear scope.

Each compact identity stores the confirmed generation at peer bytes 9..12
(little endian). R1–R4 FLBD payload bytes 68..71 migrate into this field;
R1/R2 zero means **unknown/stale**. Newly created OUR_SEC starts at the current
generation; rewrites of a retained record preserve its generation. Deleting
and recreating a bond starts current, including a same-address re-pair. R5
keeps generation **3**, attribute handles, Service Changed decisions and the
Apple flow. No schema bump accompanies capacity alone.

After encryption, bonded-state restoration and completion of SMP, the host
looks up the connected identity's generation and actual 0x2A05 CCCD. Only a
mismatch plus the indication bit enables `ble_gatts_indicate_custom` to that
connection with explicit bytes `01 00 ff ff` (range 0x0001..0xffff). The range
includes every service and a cached old GAP name. At most one indication is
outstanding per connection. Only `BLE_HS_EDONE` for that Service Changed
indication marks the peer current; queue acceptance, a notification, timeout,
failed transmission, discovery, or a CCCD write cannot do so. Failure before
queue acceptance retries in main; disconnect before confirmation leaves the
peer stale for its next connection. A peer without the CCCD indication bit
stays stale and can receive the change after it enables that bit. Its CCCD is
never enabled on its behalf.

Confirmed metadata becomes dirty in RAM immediately and is committed through
OBJ_BLE after disconnect, before advertising resumes. The existing rule
forbidding flash erase/program while linked still applies. A hard power cut
before that idle commit can repeat a confirmed indication on the next boot;
an unconfirmed change is never silently marked current. A failed idle commit
also keeps pending state for retry. Reconnect after a successful commit is
quiet until another database generation change.

R2 used `ble_svc_gatt_changed`, which calls `ble_gatts_chr_updated` for **all**
peers and persists a `value_changed` bit for disconnected bonded subscribers.
On a later boot NimBLE restores that bit at encryption, before the application
worker runs. The built-in Service Changed range is zero-initialized at boot;
that replay can therefore use 0x0000..0x0000, then race the application's
full-range indication. R3 masks only that legacy replay bit when supplying
Service Changed CCCDs to NimBLE and never calls the global invalidation API.
CCCD flags still restore normally; the per-identity generation is authoritative.
This fixes a concrete server-side range/timing hazard and cross-peer fanout.

The BlueZ bench alone does not establish the exact cause of its missing MIDI
service. DIS was appended in R2, **not inserted before MIDI**. R1's last-service
MIDI discovery end could be 0xffff; in R2 MIDI ends at 0x0011 because DIS follows.
Clients must discard that old group boundary and continue discovery across
separate 16/128/16-bit UUID response pages. Full rediscovery now has explicit
simulator coverage before encryption and with an outstanding indication,
including refreshed Device Name and MIDI traffic both ways. Service,
characteristic and descriptor discovery require no encryption; only MIDI value
access is protected. No service reordering or ATT pagination defect was found
in the pinned host. Recovery of an already corrupted BlueZ cache still needs
owner verification; if the client has disabled Service Changed, the server
cannot force it to accept indications.

Database Hash (0x2B2A) and Robust Caching are deliberately not added. Conventional
Service Changed covers the existing iOS, macOS, Android and BlueZ subscribers,
including old bonds which could not have negotiated robust caching. Adding a
hash inside the GATT service would shift the existing MIDI/DIS handles; this
pinned host has no complete Database Hash/change-awareness implementation.
Claiming robust caching would require per-client features and Database Out Of
Sync behavior as well. The current choice preserves handles and needs only
one generation per identity; it does not promise repair for a client that
ignores or never enables Service Changed.

### Stable on-air handle layout

These assignments are asserted by the simulated clients. Append future services
after DIS; do not insert attributes into the GAP/GATT/MIDI prefix, including
inside the built-in GATT service.

- 0x0001..0x0005: GAP; Device Name value 0x0003, Appearance value 0x0005.
- 0x0006..0x000d: GATT; Service Changed declaration/value/CCCD
  0x0007/0x0008/0x0009; Server Supported Features value 0x000b,
  Client Supported Features value 0x000d. Robust caching is not advertised.
- 0x000e..0x0011: MIDI; declaration/value/CCCD 0x000f/0x0010/0x0011.
- 0x0012..0x001a: DIS; manufacturer/model/firmware/software values
  0x0014/0x0016/0x0018/0x001a. As the last service, its discovery group end is
  reported as 0xffff by this NimBLE pin.

### Intended first connection and bonded reconnect

The R3 first-pairing fix is removal of the unsolicited database invalidation.
It does not weaken MIDI value security. Apple's ADG §§58.9–58.10 permits
security on values/descriptors while leaving discovery public; BLE-MIDI 1.0
§§3, 5 recommends encryption and specifies an empty initial MIDI read.

For an unpaired Apple-style central, the intended order is:

1. CONNECT_IND and public service/characteristic/descriptor discovery. MTU
   exchange may occur before or after pairing; local maximum is 517, so the
   observed Apple request of 185 negotiates 185.
2. MIDI value read returns ATT Insufficient Authentication (0x05). This is the
   pairing trigger; the peripheral sends no unsolicited Security Request.
3. The central starts SMP, the user accepts, Just Works completes (SC when
   supported, otherwise legacy), encryption starts, and both directions' keys
   are retained. New bond metadata starts current.
4. The central retries the MIDI read; the encrypted response is empty. It writes
   MIDI CCCD 0x0011 = 0x0001, then encrypted MIDI may flow both ways.
5. At least 5 seconds after connection and after SMP completes, send the single
   15-ms / latency-0 / 6-s L2CAP parameter request, allowing the central's final
   choice. Busy submission retries after 1 second. **No Service Changed** is
   sent anywhere in this new-bond sequence.

The pinned host's automatically generated MIDI CCCD is public: an early CCCD
write succeeds and does **not itself trigger pairing**. It can be written
again after encryption. MIDI value reads/writes still require encryption and
notifications are held until encryption. A central that subscribes first must
still read the protected MIDI value or initiate SMP. Do not describe an early
CCCD as rejected/retried by this firmware; that is different from its protected
read. There is no bench/spec evidence to add a new descriptor security hurdle
or make the empty MIDI value public to cure the observed Apple disconnect.

For a bonded reconnect: connect, central re-encrypts with the retained LTK
(with RPA resolution when applicable), NimBLE restores that peer's CCCDs, then
R3 checks its generation after SMP is idle. A current bond stays quiet; a stale
bond receives the single full-range indication, confirms it, and re-discovers
all pages including MIDI. Those discovery reads remain public throughout.
The central may read/subscribe during this interval; no arbitrary timer delays
the change until after MIDI setup. The >=5-second L2CAP request remains
independent. A new Apple first connection and stale-firmware recovery are
separate simulator cases; hardware acceptance remains the owner's bench.

### Connection event ring (diagnostics builds)

Both `tools/fm1_remote.py --port NAME ble` and `... log` print `ble_event` rows.
The last 32 events are kept in main-context RAM across disconnects; each has a
boot millisecond timestamp, a monotonically increasing connection session ID,
the connection handle, an event name and numeric `a b c` fields. Oldest rows
are first. A snapshot is copied before USB output, because USB sends yield to
the host. Reading logs does not clear the ring. Boot clears it; it is not a
warm-reset crash record. `ble_diag = false` removes the ring and protocol hooks.

Field meanings (handles/parameters are numeric decimal in the USB output):

- `connect`: a = peer over-air address type, b = identity resolved (0/1),
  c = identity address type. No keys or private address payloads are logged.
- `disconnect`: a = low-byte reason (19 is remote 0x13), b = full NimBLE reason.
- `enc_start`, `enc_refresh`: a = HCI status, b = encryption enabled. These
  record HCI completion of the start/refresh, not the first LL encryption PDU.
- `smp_start`: a = Pairing Request opcode, b = central AuthReq byte.
- `smp_result`: a = host result, b = method (1 legacy, 2 SC), c = SMP association
  algorithm (0 Just Works). These are actual SMP transitions, not inferred
  from whichever peer was most recently written to storage.
- `mtu`: a = effective MTU.
- `cccd_write`: a = descriptor handle, b = requested value (65535 if malformed),
  c = ATT handler result. CCCD reads do not emit this event.
- `cccd_restore`: a = descriptor handle, b = restored notify/indicate flags.
- `sc_sent`: a/b = affected start/end handles, c = prior peer generation.
- `sc_confirmed`: a = current generation. Counters `gatt_invalidations`,
  `gatt_confirmations`, `gatt_peer_version` complement these rows.
- `l2cap_request`: a = requested interval in 1.25-ms units (12), b = timeout in
  10-ms units (600), c = local submission result. An actual L2CAP response is
  `l2cap_result`, a = host result (0 accepted). Submission is not acceptance.
- `conn_update`: a/b/c = successfully applied interval/latency/timeout in HCI
  units. Central-initiated changes are included.
- `att_reject`: a = value handle, b = ATT access operation, c = ATT error;
  `midi_read`: a = value handle, emitted when its empty encrypted read succeeds.

## R3 owner bench

Use `out/open-ble/OWNER-BENCH-R3.md`. Capture the event ring immediately after
each setup/disconnect; 32 events can wrap across several sessions. Existing
bonds need to be exercised before any intentional Forget operation for that
peer. R3/R4 kept two bonds; R5 keeps eight. Use the R5 procedure to test capacity
and preserve each central's keys until the expected eviction check.

## USB bench, overnight soak and diagnostics option

Use `python3 tools/fm1_remote.py --port "$FM1_USB_PORT" ble` for a compact
snapshot, or `... log` for crash/rail/log history plus all enabled diagnostics.
Both use the USB-MIDI `LG` SysEx channel and require the final `end` frame.
An incomplete snapshot is discarded and retried, up to three 8-second reads
with 250-ms pauses; all three incomplete reads produce an error. The connection
bench always writes `measurements.json` with completed measurements and
`status.json` with completion/error details, including on reconnect failure or
interruption. `LG 01` selects BLE-only output. The full and
compact readers remain available with `ble_diag = false`. Select the **exact
USB** input/output name; the BlueZ BLE-MIDI port can also contain `FM-1`.
The CLI's `--port` precedes its commands. No debug HCI Reset USB command was
added; the existing offline HCI Reset regressions still exercise resync.

On the owner's desktop only, install `mido` / `python-rtmidi`, BlueZ tools,
`btmon` and ALSA's `aplaymidi`. Pair/bond in BlueZ beforehand and check that the
BLE-MIDI ALSA destination appears and subscribes. Find the exact USB port with
mido's port-name listing and the BLE destination with `aplaymidi -l`. Close
other FM-1 log clients. Set `FM1_PEER_ADDR`, `FM1_USB_PORT`, `FM1_BLE_PORT` to
the owner's actual values. These tools require HCI raw-socket privileges
(root or CAP_NET_RAW) and do not remove bonds or flash anything.

```sh
sudo -E python3 tools/ble_conn_update_bench.py \
  --address "$FM1_PEER_ADDR" --usb-port "$FM1_USB_PORT" \
  --output out/open-ble/owner-r2-updates

sudo -E python3 tools/ble_overnight_soak.py \
  --address "$FM1_PEER_ADDR" --usb-port "$FM1_USB_PORT" \
  --midi-port "$FM1_BLE_PORT" --hours 12 \
  --output out/open-ble/owner-r2-soak
```

The connection bench uses compact readouts by default (`--full-log` uses
`fm1_remote log`), waits for an encrypted stable link with the peripheral's
parameter request complete, and measures event-counter rate and epoch while
cycling 15→7.5→11.25→15→30→15 ms plus reconnect. It retains USB snapshots,
`btmon` text/btsnoop and JSON measurements; the 95-s initial hold crosses the
original 89.478-s regression. Longer holds are selectable.

The soak requires a bonded, encrypted and MIDI-subscribed link before playing.
It sends a generated note-on/off SMF through `aplaymidi` every 5 s, requests
15/30/11.25/15 ms every 600 s, and writes samples every 60 s to `samples.jsonl`
with the raw USB snapshots alongside. It watches HCI disconnect/encryption
events, reconnects after drops and records failed operations separately from
confirmed drops. It writes `summary.json` on completion or Ctrl-C, with
connection/drop/recovery counts, update/note totals, counter deltas, observed
reboots and final health. Exit 0 means the requested duration completed with
no logged errors; inspect the health deltas and btsnoop too. A reboot is
reported explicitly; deltas sum observed segments instead of unsigned reset
artifacts. The link is left connected when the run ends. The generated notes
and Linux tools have only been tested with offline fixtures in this round.

Set `ble_diag = false` in an open app's TOML, or use the invocation override
`--ble-diag off`. Default remains on. The option removes the 20,480-byte HCI
trace, full dump, frozen snapshots, radio register/calibration snapshots,
per-kind timing/name tables, per-reason control counts, update-record table
and detailed controller/ACL traffic counters. Scheduling budgets, IRQ
accounting, crypto counters required by CCM, canaries, error/health counters,
link/parameter/MIDI totals, maximum timing and the four-gap ring remain.
Release still measures its real scheduling budget; diagnostics removal must
not disable budget enforcement. Exact final sizes and hashes for both builds
are in `out/open-ble/RESULTS-R3.md`; the final package is diagnostics **on**.

## Persistence and calibration

The original fixed seed sectors `0x94000`/`0x95000` and bond sector `0x96000`
are **not written by the open stack**. They remain application XDATA0 space.
OBJ_BLE remains an fm1-base A/B object. R5 has a **1,236-byte** payload,
including the seed, inside `PLAT_STORE_MAX = 3840`:

| Payload offset | Contents |
| --- | --- |
| 0..15 | `FMOB`, version 2, capacity 8, peer stride 144, reserved zero; replacement counter at 8..11 |
| 16..79 | Unchanged 64-byte `WLSD` seed, CRC/flags/credit/sequence included |
| 80..1231 | Eight 144-byte compact identities; unused peers are zero |
| 1232..1235 | CRC-32 of the preceding payload |

Each peer contains one normalized identity (7 B), presence bits (1 B), MRU
rank (1 B; 1 newest), confirmed GATT generation (4 B), OUR_SEC and PEER_SEC
(60 B each: size/flags/EDIV/Rand/LTK/IRK/CSRK), two CCCDs (5 B each:
value handle/flags/value_changed), and one reserved zero byte. Both CCCDs fit
for all eight peers; no ABI structures, duplicate addresses or record CRCs are
stored per direction. Only the keys marked present are retained.

The live cache in `ble_bond_capacity.inc` keeps **no saved-record shadows**. A CRC and replacement-counter
baseline detect net changes; flush canonicalizes absent fields and ranks and
skips a state equal to that baseline. Successful encrypted use or pairing moves
an identity to rank 1; only a changed order becomes dirty. Three or any number
of repeat sessions with the MRU peer, and net CCCD unsubscribe/resubscribe,
write nothing. Actual overflow selects the oldest disconnected identity,
uses the existing host deletion path (2028 first), and counts a replacement;
new peer keys still produce 2027/204E. The pessimistic SMP FULL preflight does
not delete a bond. Connected identities are excluded. A third CCCD for an
existing identity is refused rather than evicting unrelated bonds.

R1–R4's 1,108-byte FMOB v1 / eight FLBD v1 records translate **in RAM** on load.
The next ordinary seed or bond write persists v2; there is no migration-only
write. Normal boot seed advancement is retained and often supplies that write.
The old format has no use history, so initial order follows first valid record
slot, later identity newer; true LRU starts with the next use. The old R4 parser
rejects v2, including through its truncated 1,108-byte platform read. Downgrade
can lose bonds, as r8 permits, but cannot reinterpret the new bytes as keys.
See the actual `208ebfb` parser test and R4 synthetic fixture in the results.

The existing store erases the inactive sector, writes the payload from +256,
then writes its CRC-protected 32-byte header last and verifies the result.
Load validates both headers/payloads and falls back to the previous valid
copy. Its final 256 bytes remain blank, avoiding the loader update-marker
shape. No raw NimBLE structs or private radio addresses become durable keys.
Public and random-static identities (including normalized identity types)
key OUR_SEC / PEER_SEC / CCCD records. Orphan CCCDs are discarded at load/snapshot;
delete/recreate and subscribe churn that return to the durable net state do
not write. Failed commits do not advance the durable cache baseline.

Boot commits the next seed together with unchanged bonds before radio start.
After a link ends, pending bond net changes commit together with the unchanged
seed before advertising resumes when storage is idle. Whole snapshots replace
the old single-sector journal, eliminating its destructive compaction window.
An app save may synchronously pause an idle advertisement and resume it after
the save. A live or newly accepted link makes the save return -8: callers
must retry after disconnect. The RAM driver rechecks idle with IRQ exclusion
before each erase/program, so acceptance cannot race an issued write. Plain
XIP object/calibration reads never switch flash off during a connection.

The guard preserves the RAM-only completion contract: pre-issue failures are
NOT_STORED; a late-ready/readback error is UNVERIFIED. Once issued, flash is
polled from RAM until ready; beyond the extended bound it resets without
returning to XIP while busy. The reset/ROM behavior of a still-busy part is
unproved, as in r7. Entropy classes FAILED/FIRST_BOOT/CHAINED/PROVISIONED/
UNPERSISTED and provenance/credit fields are retained. Unqualified clock/R64/
RF diversifiers receive zero entropy credit. No new CSPRNG qualification is
claimed. HCI Reset, link rekeys and host reset do not write the boot seed.

**Wear:** one sector erase per boot, alternating the two sectors, plus one
per durable bond net change. This deliberately spends more wear than r7's
60-record seed log. At a hypothetical 100,000 erase cycles per sector, the
pair allows roughly 200,000 commits: about 548 years at one boot/day, 139 days
at one boot/minute, before bond writes. This is arithmetic, not qualification
of this unit's flash endurance; rapid reboot loops consume it quickly.

**Crash windows:** before the new header commits, the old whole seed/bond
snapshot wins; after it commits, the new whole snapshot wins. No period
requires an erased-only bond sector. A cut before the boot seed commit can
reuse the prior seed lineage at the next boot; physical diversifiers remain
uncredited. Pairing while linked is RAM-only until disconnect/commit, so a
power cut before that commit loses the newly changed bond. A restore of an
older OBJ_BLE can roll back the seed chain; this format is device-local
persistence, not rollback-proof secure storage. Sequence wrap of the seed is
covered; the base object's 32-bit save sequence has the existing store's
long-term wrap limit.

Calibration is read exactly from first VM `0x93000..0x93FFF` and BTIF
`0xE9000..0xE9FFF`, as r7 04 §7 specifies, without hunting app data for record
shapes. The former overlaps XDATA0 and the latter overlaps XDATA1. Both whole
sectors are therefore **read-only in open builds**, including through the raw
data API. The store map is checked for aligned, disjoint A/B pairs within
allowed store windows. Other XDATA sectors are untouched by this controller.
A calibration sector already overwritten by an app is not reconstructed:
defaults/self-calibration apply; without a valid factory-derived public
identity this port fails startup instead of inventing an address. The unit's
calibration record is never written.

## Spec-gap notes

1. r7's Felucca flash allocation and seed-log wear design become one fm1-base
   A/B snapshot object. Record semantics/readiness/boot-before-radio ordering
   stay; physical slot rotation and erase frequency change as above. This is
   an integration adaptation, not an edit to the frozen specification.
2. The base BSP raises its supply rails before startup; the radio still reads
   and reports the clock/P33 state and does not change the system PLL. The new
   supply/startup/audio environment needs fresh device timing qualification.
3. IRQ priorities use fm1-base's actual audio 3 and scan/USB 4; baseband is 5,
   TIMER0 executor 2. Shared nested accounting replaces Felucca-specific audio
   flags. Physical budgets and 25% nested-stack margin remain unproved here.
4. RAMTEXT is 32 KiB in this BSP. Trace, host ring and DMA have explicit
   sections and are counted/cleared; no orphan section placement is relied on.
5. CDC console status is mapped to the existing USB log protocol. The full
   original controller/host/security/privacy/timing counters stream in rows
   of at most 187 ASCII bytes, with continuation for longer rows. The remote
   tool allows eight seconds for a dump. No CDC function is required.
6. The host simulator/browser build has FM1B_BLE=0 for this variant; it links
   no controller and reports BLE_OFF. Native radio simulations separately
   exercise the actual host/controller. Default JieLi simulator behavior is
   retained.

## License, provenance and clean-room tooling

Owner-authored stack/port/test files carry **MIT OR MPL-2.0**, Copyright (c)
2026 Julius Vitkauskas. Existing fm1-base and BSP files remain MPL-2.0 with
original notices. NimBLE remains Apache-2.0; bundled TinyCrypt keeps BSD notices.
See `LICENSING.md`, `CREDITS.md`, `LICENSES/MIT-BLE.txt`, and the vendor pin /
change note. The radio tables have the owner's Apache-2.0 attribution verbatim
in their header, followed by each table's source: they are JieLi SDK data (one SDK
source table, the rest constants of the compiled radio libraries read by the
specification side), not independently derived. **The open application links nothing of JieLi's except those
transcribed tables.** Runtime factory calibration reads are not linked data.
The package still carries the existing three SDK package-data files; open
builds never invoke the closed SDK radio source/library-copy path.

No excluded source contents were returned to the implementer or used to
implement the port. There was a tooling exception: initial firmware builds
ran the preexisting register checker, which internally reads every device C
file, including excluded `device/radio.c`. Once identified, open/no-radio
audits were changed to skip that unlinked file. This exception is recorded
rather than claiming that no tool ever read an excluded path. The excluded
ble/ and examples/wifi/ contents and the other prohibited trees were not
consulted for implementation. There was no flashing, device access, commit
or push.

## Verification, artifacts and limits

Run from this repository:

```sh
python3 tests/run_open_ble_tests.py
python3 tests/open_ble_build_test.py   # after the open firmware build
python3 tools/build.py template --open-ble --host
template/build/host/template-open_host template/tests/open.txt template/build/host
```

Round 2's `out/open-ble/r2-all-ble-tests.log` passes S1 (including the
measured-power variant), S2, S3, S3.1, the object-store variant, and the
production diagnostics-off legacy/SC/RPA/F-28 variants under ASan/UBSan.
The pinned TinyCrypt AES source retains the preexisting shift-base sanitizer
exception; no new sanitizer exception was added. Native tests exercise real
NimBLE SMP, encrypted MIDI, maximum MTU/value expansion, Service Changed
subscription/confirmation/restoration, public GAP/DIS discovery, delayed
L2CAP requests, central update matrices, HCI resets, persistence cuts and
600-second scheduling simulations. New adapter tests run actual HAL function
bodies against a fake register bus, accepting CONNECT_IND on the first IRQ
unlock through six advertising sessions. Gap-ring tests cover wrap, nesting,
state/reason/epoch/time, maximum retention and diagnostics on/off. USB/soak
fixtures open no ports or devices.

`r2-open-build.log`, `r2-diag-off-build.log` and `r2-no-ble-build.log` pass the
RAM-only flash code, DMA/trace ranges, source-only linker, multiply/load and
register checks. `r2-build-policy-tests.log` passes selection, diagnostics
flags, the negative archive fixture and the negative application-MMIO fixture.
The exact no-BLE footprint is unchanged from R1. A JieLi-free open host build
with SDK/toolchain variables unset and its functional template run pass
(`r2-jieli-free-host-build.log`, `r2-jieli-free-host-run.log`).
`r2-update-tests.log` passes the real open package through OTA entry/update
loader simulations under ASan/UBSan; leak checking is disabled only for those
preexisting package fixtures, which intentionally retain malloc buffers.

The wider base suite was also rerun with ASan/UBSan. It passes encoder, TRS,
storage, installer, backup/tool/page and host compilation, but **does not pass
as a whole**: unchanged `include/fastmath.h:58` shifts negative integers
(fastmath/template tests), and unchanged `device/mirror.c:179` indexes beyond
its 988-byte buffer; the mirror viewer subsequently fails its session fixture.
The normal, unsanitized open simulator's functional run passes. These
preexisting base issues were not changed for this BLE port. See
`r2-base-tests.log` and `r2-host-sanitized-run.log`; the R1 mirror failure
record remains in `baseline-mirror.log`. Emscripten is still unavailable, so
no browser build is claimed.

Archived R2 sizes, SHA256 and scope notes are in `out/open-ble/RESULTS-R2.md`.
R3's sizes, SHA256, build/test logs and current owner bench are in
`out/open-ble/RESULTS-R3.md`. The final `template/build/template-open.fwsc`
is R3 diagnostics on; its map/symbols/bin are alongside it. Packaging alone
reads the three opaque SDK files explicitly authorized by the owner. The
application link contains no SDK objects, archives or closed shim.

Known inherited limits include A9 disconnect provenance still open; Android
choosing legacy by its own SMP negotiation (BlueZ SC also supported); D-1
MIC-tail handling; peer-only privacy/D-6 (no local RPA); unknown RF anchor latch
behavior/A1/A7/B22; slow pairing consent; unqualified entropy and RF range /
temperature. The journal crash window is replaced here, with the pre-commit
bond loss and boot-seed rollback windows stated above. Hardware still has to
prove interrupt/CCM/flash budgets, audio/USB coexistence, stack margin, supply
behavior, reconnect/update behavior and USB log delivery on this integration.

## Historical R2 owner bench checklist

The completed R2 evidence is in `out/open-ble/BENCH-R2.txt`. The following is
the original broader matrix; use `out/open-ble/OWNER-BENCH-R3.md` for this round.

Keep the firmware SHA256, phone/Mac/Linux model and OS version, app version,
UTC times, USB logs, `btmon`/PacketLogger captures and the result of each item.
Use a new output directory per session; keep an initial `ble` and full `log`.

1. **Boot and basic MIDI:** owner installs the identified diagnostics-on
   package. Require READY, selftest 1, no controller error/canary failure,
   stable public identity and MIDI UUID. Pair legacy Android and SC BlueZ,
   record method/key size/bond/encryption/subscription/MTU and MIDI both ways.
2. **Gap check, five cycles:** establish an encrypted, subscribed link **before
   each** off/on cycle (REC held about 1.1 s in the template). Capture `ble`
   before off, while off, after on and after reconnect. Verify advertising
   events freeze while off; link terminates cleanly; the next session uses
   the boot identity, ADV_IND/data/parameters and accepts its first connect.
   Retain all `worker_gap` / `worker_gap_max` rows and session seen/accepted
   counts. Any >7.5-ms live/unknown gap is a failure to investigate; record
   idle flash gaps separately, with epoch and state. Repeat five times.
3. **F-28, phone:** retain the FM-1 bond, forget only on the phone and re-pair.
   Repeat for retained identity/IRK and, where controllable, changed IRK/new
   RPA. Require replacement without negative LTK loops, an encrypted 16-byte
   link, MIDI both ways and durable reconnect after disconnect/power cycle.
   Capture SMP/identity-distribution evidence; a UI pairing name is not enough.
4. **Connection updates:** run the desktop bench at 7.5 / 11.25 / 15 / 30 ms,
   both directions, then reconnect. Where central controls allow, cover
   WinOffset zero/mid/max and legal maximum windows. Hold each interval >120 s
   under MIDI/audio/USB load; keep receipt/program/instant/outcome, stable
   epoch, event-counter rate and wall time. Require no late instant,
   supervision loss, MIC failure or frozen event counter.
5. **Ten-minute load:** encrypted, subscribed MIDI both ways at 7.5 and 15 ms
   as supported, with synth/UI/USB audio active. Keep start/end and 60-s logs,
   note loss/stuck notes, drop/MIC/reset counts, gap records, timing maxima,
   canaries, audio late/xruns and stack margin. Then run the overnight tool.
6. **SAVE / boot / crash inhibit:** SAVE while advertising must pause/resume
   cleanly and attribute flash gaps; SAVE while linked must refuse with -8
   without disrupting MIDI. Save off/on settings and reboot to exercise
   APP_BLE_AT_BOOT both ways. Using the owner's established crash fixture,
   verify a warm fault/watchdog inhibits automatic radio restart and explicit
   on resumes it; cold power-on and clean exits must not be crash-inhibited.
7. **Two-peer replacement:** bond A and B, replace the oldest with C, then
   reconnect B/C (including fresh RPAs) with no prompt and encrypted MIDI.
   Confirm A needs re-pairing and no stale CCCD/IRK/key survives replacement.
   Check one atomic bond snapshot completes after disconnect and survives
   power loss; record bond_pending/writes/failures and peer identities safely.
8. **Apple — iPhone:** record iPhone/iOS and BLE-MIDI app versions. Connect
   through an iOS BLE-MIDI app or GarageBand's Bluetooth MIDI controls. Verify
   discovery, empty MIDI read after pairing, Just Works/bonding, encrypted MIDI
   both ways, MTU and subscription. Record the peripheral's first request
   (15/15 ms, latency 0, timeout 6 s, no earlier than 5 s) and the actual
   central-selected interval; distinguish accepted 30 ms from the requested
   value. Exercise screen lock/background, out-of-range recovery, five off/on
   cycles, reboot/RPA reconnect without prompts, and forget/re-pair F-28.
9. **Apple — Mac:** record Mac/macOS version. Open **Audio MIDI Setup → MIDI
   Studio → Bluetooth**, connect FM-1 Open BLE, and use Test MIDI Setup and a
   MIDI app/DAW to verify both directions. Repeat bonding/reconnect, intervals,
   MTU, subscription, sleep/wake and device off/on; retain PacketLogger or
   other available Bluetooth traces plus USB counters and audio observations.
10. **Apple — GATT cache upgrade, both:** retain the R1 bond, upgrade to the
    identified R2 firmware using the owner's normal process, reconnect without
    forgetting, and capture restored Service Changed CCCD plus a full-range
    indication and confirmation. Require rediscovery including the new DIS,
    correct GAP name, no stale MIDI handles and working encrypted MIDI.
    Disconnect before confirmation and repeat reconnect/power-cycle; the
    indication must recur. If the old client never subscribed, record that
    fact, forget/re-pair once, then repeat with subscription enabled. Record
    `gatt_invalidations`, discovery handles, CCCD and confirmation evidence.

## R5 capacity and footprint

R5 raises the shared resolving list from 4 to **10** in both deployments.
Resolution retains the R4 resumable quantum of one entry / at most one AES
block per executor item; only the next item after completion formats the
connection event. Reset clears the enlarged list in 128-byte slices, preserving
the existing per-item bound. The full-list model checks both a miss and a last
entry match (9 blocks with the local zero-IRK entry, and 10 without it,
across 10 items), with a deliberately pessimistic 52 us/AES
cost: maximum 1,260 ticks (52.5 us). This is a model, not new hardware timing.
The R4 measured maximum was 355 ticks per resolution item; batching nine such
items could take about 133 us, so no whole-list batching is introduced.

Both `ble` and `log` print `bond_count`, `bond_capacity`, persistent
`bond_replacements`, and `bond_identity ADDRESS type T rank R SC|legacy cccd
HANDLE=FLAGS cccd HANDLE=FLAGS`. Rank 1 is MRU. CCCD handles are characteristic
value handles (Service Changed 0008, MIDI 0010 for this database); flag 0002
indicates, flag 0001 notifies, and handle 0000 is absent. No keys are printed.
`log` also reports bond writes/erases/pending/failures, resolving-list size and
entries, LTK negatives and per-kind executor timing. With diagnostics off,
identity metadata and capacity still appear in the compact `ble` readout.

R4→R5 deltas (bytes; pool includes DMA and the host pools):

| Diagnostics | App flash | .data + .bss | Pool | Total data RAM |
| --- | ---: | ---: | ---: | ---: |
| on | +2064 | 0 | +208 | +208 |
| off | +2080 | +4 | +208 | +212 |

The unchanged trace adds 20480 B on / zero off. RAMTEXT code remains 3112 B.
Full R4/R5 sizes and package hashes are in RESULTS-R5. `template-open-r5.fwsc`
is the diagnostics-on owner image; `template-open-r4.fwsc` remains unchanged.
Felucca's sector-journal host retains **N_BOND=2**, permitted by r8; it shares
the ten-entry list, one-entry resolution steps and sliced Reset clearing.
