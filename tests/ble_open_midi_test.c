/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* Compiles the actual platform MIDI functions, extracted by the runner. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../include/plat.h"
#define RING_PUBLISH() do {} while(0)
#define MQ 8u
static uint32_t now,mi_w,mi_r,midi_in_q[MQ],usb_room,bt_room,last_usb,last_bt,bt_in;
static int in_tick,bt_connected;
static struct { int config,suspended; } usb;
uint32_t plat_ms(void) { return now; }
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t f) { (void)f; }
int plat_ble_connected(void) { return bt_connected; }
static uint32_t midi_out_room(void) { return usb_room; }
static uint32_t midi_out_link(void) { return 7; }
static uint32_t ble_midi_out_room(void) { return bt_room; }
static void midi_out_event(uint32_t p) { last_usb=p; }
static void ble_midi_tap(uint32_t p) { last_bt=p; }
static int ble_midi_in(uint32_t *p) { if(!bt_in) return 0; *p=bt_in; bt_in=0; return 1; }
#include "midi_seam.inc"
int main(void)
{
    uint32_t p; int port;
    assert(!plat_midi_out_room(TO_USB|TO_BLE));
    usb.config=1; usb_room=23; assert(plat_midi_out_room(TO_USB|TO_BLE)==23);
    bt_connected=1; bt_room=3; assert(plat_midi_out_room(TO_USB|TO_BLE)==3);
    bt_room=0; assert(!plat_midi_out_room(TO_USB|TO_BLE));
    bt_room=3; usb_room=0; assert(!plat_midi_out_room(TO_USB|TO_BLE));
    usb.suspended=1; assert(plat_midi_out_room(TO_USB|TO_BLE)==3);
    now=123; plat_midi_send(0x643c9009,TO_USB|TO_BLE);
    assert(last_usb==0x643c9009 && last_bt==last_usb);
    assert(plat_midi_count(PORT_BLE,1)==1 && plat_midi_ago(PORT_BLE,1)==0);
    plat_midi_send(0x0000f80f,TO_BLE); assert(plat_midi_count(PORT_BLE,1)==1);
    bt_in=0x643c9009; assert(plat_midi_in(&p,&port) && port==PORT_BLE && p==0x643c9009);
    assert(plat_midi_count(PORT_BLE,0)==1 && !plat_midi_in(&p,&port));
    midi_in_q[mi_w++%MQ]=0x643c9019;
    assert(plat_midi_in(&p,&port) && port==PORT_TRS && p==0x643c9009);
    now=131; assert(plat_midi_ago(PORT_BLE,0)==8);
    puts("actual platform MIDI: mixed/full/absent queues, USB/BLE packets, input port, counters/realtime and activity: PASS");
    return 0;
}
