# R5 — bond capacity, frozen r8 (2026-10-22)

Implemented uncommitted above fm1-base `open-ble` **208ebfb**, with the shared
controller port uncommitted above Felucca `ble-controller` **88d426f**.
The R4 five-central Apple bench and its 355-tick resolution maximum were read.
No devices or bench network hosts were accessed; no flashing, commits or pushes
were performed. The owner's running R4 soak was not disturbed.

The fm1-base deployment now keeps **8 peer identities, both security directions
and both CCCDs per peer**, with persistent LRU replacement, in **1,236 bytes
including the seed**. The controller resolving list is **10** in both trees.
Felucca's journal host remains **N_BOND=2**, as r8 explicitly permits.

R3/R4 attribute handles, database generation **3**, Service Changed policy,
Apple first-pair flow, MIC journal, encryption and controller timing remain.
The necessary controller capacity change also slices the enlarged resolving
list's Reset clearing; resolution already had a one-entry resumable quantum
in R4, which is preserved. Physical R5 acceptance remains for the owner.

## Stored layout and RAM

`open_ble/ble_store.{c,h}` retains the existing `OBJ_BLE` A/B sectors and
`device/storage.c` payload-first/header-last transaction without changing the
platform storage implementation or sector map. Every size below is bytes;
integers are explicitly little endian, with no persisted ABI structures.

| Object offsets | Size | Content |
| --- | ---: | --- |
| 0..3 | 4 | `FMOB` |
| 4 | 1 | Version **2** |
| 5 | 1 | Peer capacity **8** |
| 6 | 1 | Peer stride **144** |
| 7 | 1 | Zero |
| 8..11 | 4 | Persistent replacement counter, modulo 2^32 |
| 12..15 | 4 | Zero |
| 16..79 | 64 | Unchanged `WLSD` seed, its CRC/credit/sequence included |
| 80..1231 | 1152 | Eight compact identities |
| 1232..1235 | 4 | CRC-32 over bytes 0..1231 |
| **Total** | **1236** | **2604 below PLAT_STORE_MAX 3840** |

| Peer offsets | Size | Content |
| --- | ---: | --- |
| 0..6 | 7 | Normalized identity type + six address octets |
| 7 | 1 | OUR_SEC / PEER_SEC / CCCD0 / CCCD1 presence bits |
| 8 | 1 | LRU rank: **1 MRU**, largest rank LRU |
| 9..12 | 4 | Confirmed GATT database generation |
| 13..72 | 60 | OUR_SEC |
| 73..132 | 60 | PEER_SEC |
| 133..137, 138..142 | 10 | Two CCCDs: value handle(2), flags(2), value_changed(1) |
| 143 | 1 | Zero |

Each security direction stores size(1), presence/authentication/SC flags(1),
EDIV(2), Rand(8), LTK(16), IRK(16), CSRK(16). Only keys marked present are
copied; absent fields and unused peers canonicalize to zero. Compared with
four old 128-byte records per peer, the stored form is **144 vs 512 bytes**.
All 32 logical records fit simultaneously for eight peers. No raw NimBLE
structs, record padding or repeated identity address is stored per direction.

The live cache in `ble_bond_capacity.inc` has **no saved-record shadow**:
CRC-32 of its canonical bytes plus a saved replacement count detect net
changes. Inactive fields remain only until flush to recognize delete/recreate
no-ops, then are zeroed. The existing verified object cache remains for seed
preservation and transactional readback, not for per-record change detection.
The A/B baseline advances only after successful save/readback. Failed commits
retain pending state and can be retried. Snapshot preparation runs in main,
with the controller interruptible, and flash still requires platform idle.

## Use order, replacement and diagnostics

- Pairing/new security creation and successful encryption with a stored key
  promote that identity to MRU. Failed encryption does not. Moving an already
  MRU peer changes nothing. Rank gaps close without changing use order.
- Disconnect flush commits the net identity/security/CCCD/order state and
  replacement count together. Equal net state writes nothing. RPA-keyed writes
  remain refused; orphan CCCDs are dropped at load/snapshot. Per-link method
  diagnostics still read that peer's identity record, never the latest write.
- NimBLE's pessimistic `BLE_STORE_EVENT_FULL` preflight returns success without
  evicting. Actual `OVERFLOW` selects the largest-rank **disconnected** identity,
  excluding incoming/existing peers and every connection found by the host.
  It deletes through the existing `ble_host_delete_bond`: **2028 before store
  deletion**, followed by normal new-peer **2027 / 204E**. Successful deletion
  increments the persistent replacement counter. If no victim is eligible,
  the write fails; it does not delete a connected bond.
- Each peer owns two CCCD slots. A third CCCD for an existing peer returns
  capacity without deleting another peer; an invalid zero handle returns
  EINVAL before overflow handling, so it cannot cascade evictions.
- A recreated OUR_SEC starts at generation 3, including same-address re-pairing
  with a RAM tombstone; a retained security rewrite keeps its old generation.
  Existing R3 cache-confirmation and indication retry behavior is unchanged.

`ble` and `log` report `bond_count`, `bond_capacity`, `bond_replacements` and
`bond_identity ADDRESS type T rank R SC|legacy cccd HANDLE=FLAGS cccd
HANDLE=FLAGS`. Identity metadata is frozen before USB output yields. No keys
are copied into this diagnostic structure or printed. Rank 1 is MRU; handles
are characteristic **value** handles (0008 Service Changed, 0010 MIDI), flags
0002 indicate / 0001 notify; handle 0000 is absent. The compact `ble` readout
retains identity/capacity diagnostics with `--ble-diag off`. Full diagnostics
also retain writes/erases/pending/failures, resolving-list counts, HCI trace,
LTK-negative count and executor timing.

## Upgrade and downgrade

R1–R4 use the same **1,108-byte FMOB v1 / eight FLBD v1** record layout. Load
validates the old outer CRC/header and individual records, translates both
security directions, all key properties, CCCDs and generations into RAM, and
keeps the seed unchanged. There is **no migration-only boot write**. The next
ordinary seed or bond save writes v2; the existing boot seed advance usually
supplies that save. An R4 bond already evicted before upgrade cannot be recovered.

The old format stores no use history. Initial migration order is deterministic
by first valid record slot, later identity newer. The next successful use
establishes actual LRU; inventing pre-upgrade recency would be impossible.

`tests/fixtures/r4-bonds-object.bin` is synthetic, deterministic test data:
valid old seed, two identities, all eight old records, both directions and all
keys, public/static addresses, SC and legacy, and generations 3 and 0. Tests
verify every field before/after load, ordinary seed migration and byte cuts.
No device key material was read to create it.

For downgrade, the host runner obtains **unmodified `ble_store.c/.h` from git
208ebfb**, compiles them under ASan/UBSan with renamed entry points, and feeds
the actual new object through the real platform's truncating 1,108-byte read.
R4 rejects version 2 and returns an empty seed/all FF bond records, without
writing or interpreting new bytes as keys. Downgrade may lose bonds as r8
permits; it does not crash or misread them.

## Resolving list and W_item

Both `ble_privacy.h` files define **N_RL=10**. R4 already yielded after one
entry / at most one AES block, so no whole-list AES loop is introduced. List
acceptance comparisons grow to ten; connection-event formatting happens only
in the following item after final resolution. HCI Reset clears the enlarged
424-byte privacy state in **128-byte slices**; leaving the old synchronous
clear exceeded the existing pessimistic Reset test's 70-us check. All Reset
ordering/stale-event tests pass with the split clearing.

The R4 log's measured `resolution` maximum is **355 ticks = 14.79 us**, from
an implementation already processing one entry per item. Batching nine such
items would estimate **3195 ticks = 133.13 us**, beyond 100 us. Independently,
the HAL gives an entire AES transaction a shared 26-us deadline; nine worst
transactions could consume 234 us before other work. Keeping one entry per
item avoids assuming that normal-case hardware timing bounds a full scan.

The full-list test models **52 us per block**, deliberately twice the HAL's
shared deadline, plus one tick per controller memory byte. With the profile's
local zero-IRK entry: nine blocks in ten items; with all ten IRKs nonzero:
ten blocks in ten items. Both miss and last-entry match pass, each item does
at most one block, and the event is deferred. Maximum modeled resolution item:
**1260 ticks = 52.5 us**, below 2400 ticks / 100 us. These are scheduling-model
checks, **not new target timing measurements**; the owner must confirm silicon
W_item and the longer receive-interrupt list comparison on hardware.

## Validation

All C suites use **ASan + UBSan**, warnings as errors. The inherited upstream
TinyCrypt AES `shift-base` sanitizer exclusion remains local to that source;
controller, host integration, object store and HAL tests are instrumented.

- **F-29 real-host simulation:** eight actual mixed SC/legacy SMP pairings,
  both CCCDs enabled after pairing, controller HCI and real CCM over the real
  A/B store on byte-cuttable simulated NOR. Cache reload + host HCI resync
  restores all eight; reconnect order 4,2,8,1,6,3,7,5 produces exact ranks.
  Three more peer-5 sessions perform **zero programs and zero erases**.
  Ninth actual pairing evicts **only peer 4**; second reload preserves order
  and replacement count 1. Peer 4's fresh RPA remains unresolved and receives
  a **real negative HCI LTK reply**; all other eight retain keys, both CCCDs,
  RL identities, encrypted reconnects and MIDI subscriptions. The test also
  checks third/invalid CCCD failures cannot evict unrelated bonds.
- **F-28 / R3 clients:** real same-IRK and changed-IRK repeat pairing, including
  new unresolved RPA; 2028 before 2027/204E without disconnect; fresh-RPA
  encrypted reconnects, Apple public discovery/protected read/SMP/CCCD flow,
  new-bond quietness, stale generation migration, unconfirmed retry and
  confirmed quietness. Added overflow while the oldest peer is connected
  verifies that peer survives and the oldest disconnected peer is replaced.
- **Migration/downgrade:** full R4 fixture, zero load/flush writes for migration
  alone, normal boot seed migration, actual R4 parser clean rejection.
- **21,460 byte-cut cases:** 5,365 each for seed change, bond change, v1→v2
  migration and full eight-peer replacement. Erase/payload/commit-header cuts
  yield whole old/new state. All 32 logical records, LRU, counter and seed
  remain atomic; loader-marker tails stay blank. Additionally, **1,236 single
  byte corruptions and 1,236 payload truncations** are rejected as empty.
- **H-22/H-23** fill all ten entries, overflow, duplicate identity/IRK, absent
  removal/privacy mode, and invalid enables. H-24/H-25/K-11/RPA and full-list
  resumable timing cases pass in both trees.
- **Existing suites:** platform/MIDI/storage seams, diagnostics on/off, S1,
  S2, S3, S3.1, connection updates, entropy, shutdown/Reset and MIC directed
  tests all pass. **10,000,000 randomized MIC events in each tree** pass with
  2,280,220 accepted encrypted PDUs and zero spontaneous MIC failures.
- Both final target builds pass source-only linking, no vendor archives/shim,
  RAM-only flash code, DMA bounds, multiply/load erratum, no soft double,
  footprint and MMIO checks; four build-policy tests pass.

Reproduction and evidence:

```sh
CC=/root/fm1/fm1-base-open/out/open-ble/cc-sanitize \
 python3 tests/run_open_ble_tests.py
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
 python3 tests/run_ble_s3_tests.py --s31 --open-store
# Repeat that command with --no-diag for the release store/integration cases.
python3 tests/open_ble_build_test.py
```

`TESTS-R5.log` is the complete aggregate and 10M run. `STORE-R5-FINAL.log` and
`STORE-R5-DIAG-OFF-FINAL.log` verify the final store, full fixtures/cuts, F-29
and all host integrations after final store guards/cache-generation fixes.
`BUILD-R5.log`, `BUILD-R5-DIAG-OFF.log`, `BUILD-POLICY-R5.log` cover final builds.
Felucca: `out/ble-r5-{s1,s2,s3,s31,mic}-tests.log`; its host stays two peers.

## Footprint and artifacts

| Diagnostics | Metric, bytes | R4 | R5 | Delta |
| --- | --- | ---: | ---: | ---: |
| on | Flash app image | 195,648 | 197,712 | +2,064 |
| on | Static .data + .bss | 34,584 | 34,584 | +0 |
| on | Pool (includes DMA/heap/host pools) | 101,212 | 101,420 | +208 |
| on | Trace RAM | 20,480 | 20,480 | 0 |
| on | **Data + pool + trace** | **156,276** | **156,484** | **+208** |
| off | Flash app image | 184,624 | 186,704 | +2,080 |
| off | Static .data + .bss | 33,756 | 33,760 | +4 |
| off | Pool (includes DMA/heap/host pools) | 99,228 | 99,436 | +208 |
| off | Trace RAM | 0 | 0 | 0 |
| off | **Data + pool + trace** | **132,984** | **133,196** | **+212** |

Total static data growth, including the pool and unchanged trace, is only
**208 B on / 212 B off**, below the ~2 KB target. Pool symbols explain it:
`bonds` 1364→1200 (-164), `object` 1108→1236 (+128), `rl` 184→424 (+240),
net +204 plus 4 alignment bytes = **+208**. The 4096-byte NimBLE arena and
packet-pool geometry are unchanged. DMA is 4340 B, already in the pool;
RAMTEXT code remains 3112 B. The seed commit's two temporary object buffers
grow by 256 B combined; old-format migration temporarily uses a 1108-B main
stack buffer. No stack, heap or packet-pool reservation is enlarged.

The app image is the flash figure; the unchanged loader packages to 6980 B.
FWSC packages remain 610136 B because of their fixed layout. R4 comparison
uses preserved R4 maps/symbols and `BUILD-R4{,-DIAG-OFF}.log`. Both final R5
maps, symbols and binaries are copied beside each package.

The required build was run, then the image copied:

```sh
JIELI_TOOLCHAIN=/root/felucca-ble-env/jieli/jieli-linux-toolchains-20260730.1 \
 AC79_SDK=/root/sdk-blemidi JIELI_DOCKER=0 \
 python3 tools/build.py template --open-ble
cp template/build/template-open.fwsc template/build/template-open-r5.fwsc
```

Diagnostics-off validation adds `--ble-diag off --package-name
template-open-r5-no-diag.fwsc`. The R4 package was never overwritten or deleted.

- Owner image: [template-open-r5.fwsc](../../template/build/template-open-r5.fwsc). SHA256: `ebf0dfb3b7ff0b75e37417fceb0998ad6d7917153c09ac90362e33222106986f`.
- Diagnostics off: [template-open-r5-no-diag.fwsc](../../template/build/template-open-r5-no-diag.fwsc). SHA256: `049117a52063e83c92461e97fc47e329615c9b3f7faf616c287b4ef26e6d134e`.
- R4 preserved SHA256: `8866ab78df218f4a9c89c4b409e9fce9c3b2d4d4b49aeb2574ef94f85ed6fe06`.
- [R5-felucca.patch](R5-felucca.patch): shared controller/tests and deployment documentation only; reverse-apply check passes against the uncommitted tree. SHA256: `c3843fb5d64217cc74194751d73ba9cb366f6fd46297c18066769df81d80c8a0`.
- [OWNER-BENCH-R5.md](OWNER-BENCH-R5.md): retained-bond upgrade, five real centrals + static-address BlueZ identities, exact LRU order, zero-write repeats, victim/survivor checks and adapter/key-state restoration.
- Updated [docs/OPEN-BLE.md](../../docs/OPEN-BLE.md); package/patch hashes also in [SHA256-R5.txt](SHA256-R5.txt).


## r8 clarifications, deviations and limits

1. r8 retains older profile rows saying two bonds / N_RL≥4; its new deployment
   requirements take precedence here: **fm1-base 8 / 10**, Felucca **2 / 10**.
2. `N_RL-1` AES blocks assumes the profile's zero-peer-IRK local entry. HCI can
   legally fill all ten slots with nonzero peer IRKs. Both cases are supported
   and tested, rather than silently treating nine as a universal limit.
3. Old use order is absent from v1, so initial migrated order is the documented
   slot-order fallback. Keys and subscriptions are retained; old recency cannot
   be reconstructed. r8 gives no initial-order rule for this case.
4. Durability is at the **disconnected idle flush**, as F-29 instructs. A power
   cut before that flush can lose a new bond or a just-changed LRU order, just
   as R4 could lose a new unflushed bond. Literal immediate durability during
   a live link would conflict with the unchanged platform no-live-flash rule.
   No additional live-link flash writes are introduced.
5. No intended functional deviation from r8 under those interpretations. The
   owner hardware F-29, actual Intel static-address behavior, silicon W_item
   and IRQ bounds, and R4 soak outcome are not claimed by host tests. The
   bench guide verifies the identity on FM-1 before counting it and keeps
   independent desktop key state, then restores the original adapter state.
