# Credits

- **Leo Kuroshita** ([@kurogedelic](https://github.com/kurogedelic), [Hügelton Instruments](https://hugelton.com)):
  Felucca, the firmware this base grew from, and felucca-bsp, which it is built on. Much of fm1-base is his code,
  used under MPL-2.0 with his permission ([hugelton/felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4)).
- **Charles Vestal** ([@charlesvestal](https://github.com/charlesvestal)): fm1-base, and the platform layer of the
  FM-1 firmwares it comes from (X0X, FoMni-1, CTL-1 and the others).
- **keremimo** ([@keremimo](https://github.com/keremimo)): the MIDI clock and transport sync in felucca-bsp's
  `usb.c` ([hugelton/felucca-bsp#3](https://github.com/hugelton/felucca-bsp/pull/3)).
- The idea of running FM-1 firmware in the browser: [Groove OS](https://www.groove-os.com/) by Peter Gombos (no
  code from it).
- Fonts: Barlow by The Barlow Project Authors and Terminus by Dimitar Toshkov Zhekov (both SIL OFL 1.1).

- **Julius Vitkauskas**: the clean-room AC79/WL82 open BLE controller, BLE-MIDI,
  NimBLE platform integration, persistence and native tests (`open_ble/`,
  `device/ble_open*.c`), MIT OR MPL-2.0.
- **The Apache Software Foundation and NimBLE contributors**: Apache NimBLE
  1.9.0, Apache-2.0; **TinyCrypt contributors**: cryptographic primitives,
  BSD-3-Clause with the notices retained in `third_party/nimble/`.
- **JIELI (Zhuhai Jieli Technology Co., Ltd.)**: the transcribed radio table
  values in `open_ble/hal/fm1_ble_tables.h`, Apache-2.0. The open build links
  nothing of JieLi's except these tables; its package still carries the three
  existing SDK package-data files. See [LICENSING.md](LICENSING.md).
