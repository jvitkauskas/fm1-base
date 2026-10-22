/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
#pragma once
#ifndef FM1B_BLE_DIAG
#define FM1B_BLE_DIAG 1
#endif
#if FM1B_BLE_DIAG
#define BLE_DIAG_ONLY(...) do { __VA_ARGS__; } while(0)
#else
#define BLE_DIAG_ONLY(...) do {} while(0)
#endif
