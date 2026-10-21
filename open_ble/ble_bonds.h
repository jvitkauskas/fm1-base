/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
#ifndef FELUCCA_BLE_BONDS_H
#define FELUCCA_BLE_BONDS_H
#include <stdint.h>
union ble_store_key;
union ble_store_value;
typedef struct {
    uint32_t writes,erases,failures,malformed,private_refused,private_dropped,orphan_dropped,net_noops;
    uint8_t pending,records;
} ble_bond_diag_t;
void ble_bonds_init(void);
void ble_bonds_flush(void);
void ble_bonds_get_diag(ble_bond_diag_t *out);
int ble_bonds_read(int type,const union ble_store_key *key,union ble_store_value *out);
int ble_bonds_write(int type,const union ble_store_value *value);
int ble_bonds_delete(int type,const union ble_store_key *key);
#endif
