/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* Macro definitions come from the real BSP through its C preprocessor. */
#include "flash_ranges.inc"
int main(void)
{
    assert(!FL_RANGE_OK(0x93000u,4096u));
    assert(!FL_RANGE_OK(0xe9000u,4096u));
    assert(!FL_RANGE_OK(0x92fffu,2u));
    assert(!FL_RANGE_OK(0xe8fffu,2u));
    assert(FL_RANGE_OK(0x94000u,4096u) && FL_RANGE_OK(0x95000u,4096u) && FL_RANGE_OK(0x96000u,4096u));
    assert(FL_RANGE_OK(0xde000u,8192u));
    assert(FL_RANGE_OK(0xfc000u,8192u));
    assert(FL_RANGE_OK(0xe0000u,4096u));
    assert(!FL_RANGE_OK(0xffffffffu,2u));
    assert(!FL_RANGE_OK(0x97000u,0xffff0000u));
    puts("actual BSP flash ranges: calibration/BTIF protected, former seed/journal XDATA free, A/B store allowed, overflow refused: PASS");
    return 0;
}
