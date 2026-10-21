/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
#ifndef FELUCCA_BLE_SECURITY_H
#define FELUCCA_BLE_SECURITY_H
#include <stdint.h>
enum { BLE_ENC_N, BLE_ENC_S1, BLE_ENC_S2, BLE_ENC_PA, BLE_ENC_PB, BLE_ENC_PC };
enum { BLE_MARK_RX_ON=1, BLE_MARK_RX_OFF, BLE_MARK_REJECT_ACK };
typedef struct {
    uint64_t tx_counter,rx_counter;
    uint32_t mic_failures,phase_errors,starts,refreshes,ltk_requests,negative_replies;
    uint8_t phase,tx_on,rx_on,encrypted;
} ble_security_diag_t;
void ble_security_get_diag(ble_security_diag_t *out);
uint8_t ble_ll_ltk_reply(uint16_t handle,const uint8_t *ltk,int negative);
int ble_ll_tx_prepare(unsigned id,uint8_t out[31],uint8_t *n);
uint8_t ble_ll_tx_boundary(unsigned id);
int ble_ll_rx_reserve(unsigned pdus,unsigned markers);
int ble_ll_rx_after_marker(uint8_t marker,uint16_t status,uint16_t header,const uint8_t *data);
#ifdef BLE_HOST_TEST
void ble_security_test_counters(uint64_t tx,uint64_t rx);
#endif
#endif
