/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
#ifndef FELUCCA_BLE_MIDI_H
#define FELUCCA_BLE_MIDI_H
#include <stdint.h>
typedef void (*ble_midi_byte_fn)(uint8_t byte,uint16_t timestamp,void *arg);
typedef struct { uint8_t sysex; } ble_midi_decoder_t;
/* Running status is local to each BLE packet; SysEx may span packets. */
int ble_midi_decode(ble_midi_decoder_t *s,const uint8_t *p,unsigned n,ble_midi_byte_fn emit,void *arg);
int ble_midi_encode_usb(uint32_t packet,uint32_t ms,uint8_t *p,unsigned cap);
typedef struct { uint8_t status,n,need,bytes[3],sysex,sx[3],sx_n; } ble_midi_usb_t;
typedef void (*ble_midi_usb_fn)(uint32_t packet,uint16_t timestamp,void *arg);
void ble_midi_usb_byte(ble_midi_usb_t *s,uint8_t byte,uint16_t timestamp,ble_midi_usb_fn emit,void *arg);
#endif
