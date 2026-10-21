<!-- SPDX-License-Identifier: MIT OR MPL-2.0 -->
<!-- Copyright (c) 2026 Julius Vitkauskas -->
# fm1-base integration changes

The upstream subset and pin are recorded in PINNED.md. Original NimBLE and
TinyCrypt license/copyright notices remain intact.

`ext/tinycrypt/src/ecc.c` has one conditional integration change: with
`FM1B_BLE_OPEN`, `muladd` calls the out-of-line `ble_wl82_mul32` in
`open_ble/ble_crypto.c`. The pure multiply consumes its product before the
next memory load. This follows felucca-bsp's documented pi32v2 hazard in
`bsp/docs/CPU.md`; final disassembly passes the existing multiply/load check.
Other upstream files are byte-identical to the allowed Felucca-ble pin.
The conditional additions are Copyright (c) 2026 Julius Vitkauskas,
MIT OR MPL-2.0; the original TinyCrypt BSD notices still apply.

The NPL and configuration live outside this vendor directory, in
`open_ble/port/` and `open_ble/ble_npl.c`. The host privacy cache and repeated
pairing fixes remain application callbacks; NimBLE sources are unchanged.
