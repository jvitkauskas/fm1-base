/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* R8 complete-runtime SRAM map. The entry stub/bootstrap run from XIP and
 * copy .ram_text (all runtime text and constants) before entering C.
 * TRACE preserves the boot probe at 0x01C07F00. Stack guard bands, stacks,
 * NOINIT and vectors keep their original addresses. All executable SRAM is
 * covered by the existing _rt_start.._rt_end PC guard.
 */
MEMORY
{
    XIP (rx) : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    TRACE (rw) : ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAMTEXT (rwx) : ORIGIN = 0x01C08000, LENGTH = 208K
    RAM (rw) : ORIGIN = 0x01C3C000, LENGTH = 40K
    POOL (rw) : ORIGIN = 0x01C46000, LENGTH = 0x2E000
    NOINIT (rw) : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) KEEP(*(.boot_text)) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .;
                  *(.text*) *(.rodata*) *(.*_const)
                  KEEP(*(.ram_text*))
                  . = ALIGN(4); _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .;
                  _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .ble_trace (NOLOAD) : { . = ALIGN(4); _ble_trace_start = .; *(.ble_trace) _ble_trace_end = .; } > TRACE
    ASSERT(_ble_trace_end <= ORIGIN(TRACE) + LENGTH(TRACE), "R8 trace overlaps boot probe")
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; * (.ble_host_ring) *(EXCLUDE_FILE (*nimble_*.o) .bss* .sbss*) *(COMMON) *(.*_bss) *(.bd_base) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) *nimble_*.o(.bss* .sbss*)
              . = ALIGN(4); _ble_dma_start = .; *(.ble_dma) _ble_dma_end = .;
              . = ALIGN(4); _pool_end = .; } > POOL
    ASSERT(_ble_dma_start >= ORIGIN(POOL) && _ble_dma_end <= ORIGIN(POOL) + LENGTH(POOL), "open BLE DMA outside verified SRAM")
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
