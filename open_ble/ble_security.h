/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
#ifndef FELUCCA_BLE_SECURITY_H
#define FELUCCA_BLE_SECURITY_H
#include <stdint.h>
#include "ble_diag.h"
enum { BLE_ENC_N, BLE_ENC_S1, BLE_ENC_S2, BLE_ENC_PA, BLE_ENC_PB, BLE_ENC_PC };
enum { BLE_MARK_RX_ON=1, BLE_MARK_RX_OFF, BLE_MARK_REJECT_ACK };
typedef struct {
    uint64_t tx_counter,rx_counter;
    uint32_t mic_failures,phase_errors,starts,refreshes,ltk_requests,negative_replies;
    uint8_t phase,tx_on,rx_on,encrypted;
} ble_security_diag_t;
void ble_security_get_diag(ble_security_diag_t *out);
enum { BLE_MIC_RECORDS=4, BLE_RX_DELAYED=1, BLE_RX_REPEAT=2, BLE_RX_PRESSURE=4,
       BLE_RX_EVENT_UNKNOWN=8, BLE_RX_BOTH_FULL=16, BLE_RX_RECOVERY=32 };
#if FM1B_BLE_DIAG
typedef struct {
    uint64_t counter;
    uint8_t sn,length;
} ble_mic_pdu_t;
typedef struct {
    uint32_t serial,ms,epoch,received_ms,since_update_ms;
    uint64_t counter;
    uint16_t event,service_event,interval,header,status;
    uint8_t slot,flags,rx_count,tx_count;
    ble_mic_pdu_t rx[4],tx[2]; /* oldest first; metadata only */
} ble_mic_record_t;
unsigned ble_security_mic_records(ble_mic_record_t out[BLE_MIC_RECORDS]);
void ble_security_tx_loaded(uint8_t sn);
#else
#define ble_security_tx_loaded(sn) ((void)(sn))
#endif
uint8_t ble_ll_ltk_reply(uint16_t handle,const uint8_t *ltk,int negative);
int ble_ll_tx_prepare(unsigned id,uint8_t out[31],uint8_t *n);
uint8_t ble_ll_tx_boundary(unsigned id);
int ble_ll_rx_reserve(unsigned pdus,unsigned markers);
int ble_ll_rx_after_marker(uint8_t marker,uint16_t status,uint16_t header,const uint8_t *data);
#ifdef BLE_HOST_TEST
void ble_security_test_counters(uint64_t tx,uint64_t rx);
#endif
#endif
