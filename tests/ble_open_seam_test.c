/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../include/plat.h"
#include "../open_ble/ble_host.h"
static uint32_t now,crashes,start_calls,off_calls,on_calls;
static uint8_t ble_started,start_fail,fault,link,subscribed;
static struct { uint32_t magic,count; } fm1_crash;
static struct { uint8_t p3_rst; } fm1_boot;
#define FM1_CRASH_MAGIC 0x43525348u
static ble_host_diag_t host;
uint32_t ble_port_lock(void) { return 0; }
void ble_port_unlock(uint32_t k) { (void)k; }
static int ble_platform_start(void)
{
    start_calls++; if(start_fail) return -1;
    ble_started=host.running=1; return 0;
}
int ble_port_storage_idle(void) { return !link; }
int ble_port_failed(void) { return fault; }
void ble_host_enable(int on)
{
    if(on) on_calls++; else { off_calls++; link=subscribed=0; }
}
int ble_host_pause_advertising(void) { return link ? -8 : 0; }
void ble_host_resume_advertising(void) {}
void ble_host_worker(void) { now++; host.synced=1; }
void ble_host_get_diag(ble_host_diag_t *d) { *d=host; }
int ble_host_linked(void) { return link; }
int ble_host_connected(void) { return link && subscribed; }
uint32_t ble_host_midi_room(void) { return subscribed ? 19 : 0; }
int ble_host_midi_take(uint32_t *pkt,uint32_t *ms) { *pkt=0x643c9009; *ms=now; return subscribed; }
void ble_host_midi_out(uint32_t pkt,uint32_t ms) { assert(pkt==0x643c9009); (void)ms; }
static uint32_t fm1_ms;
#include "../device/ble_open.c"
static void reboot(int cold)
{
    fm1_boot.p3_rst=cold ? 1 : 0;
    fm1_crash.magic=FM1_CRASH_MAGIC; fm1_crash.count=crashes;
    ble_started=ble_state=ble_applied=ble_wanted=0; memset(&host,0,sizeof host);
    radio_boot_note();
}
int main(void)
{
    uint32_t pkt;
    reboot(1); assert(!radio_last_bad && plat_ble_state()==BLE_OFF);
    plat_ble_start(); assert(plat_ble_state()==BLE_STARTING); ble_poll();
    assert(plat_ble_state()==BLE_READY && start_calls==1 && open_log.active);
    assert(!ble_open_store_begin()); ble_open_store_end();
    link=1; assert(ble_open_store_begin()==-8); assert(plat_ble_linked() && !plat_ble_connected() && !ble_midi_out_room());
    subscribed=1; assert(plat_ble_connected() && ble_midi_out_room()==19);
    assert(ble_midi_in(&pkt) && pkt==0x643c9009); ble_midi_tap(pkt);
    plat_ble_enable(0); assert(!plat_ble_connected() && !ble_midi_out_room()); ble_poll();
    assert(plat_ble_state()==BLE_OFF && !plat_ble_linked() && off_calls==1 && !open_log.active);
    plat_ble_enable(1); assert(plat_ble_state()==BLE_STARTING); ble_poll();
    assert(plat_ble_state()==BLE_READY && start_calls==1 && on_calls==1 && open_log.active);
    for(unsigned i=0;i<60;i++) plat_log("ring line");
    assert(plat_ble_log_total()>=60 && !strcmp(plat_ble_log_line(47),"ring line") && !*plat_ble_log_line(48));
    unsigned total=plat_ble_log_total(); reboot(0); assert(!radio_last_bad && plat_ble_log_total()==total);
    plat_ble_start(); ble_poll(); crashes++; reboot(0);
    assert(radio_last_bad && plat_ble_state()==BLE_OFF);
    /* The main boot gate respects a prior Bluetooth crash and APP_BLE_AT_BOOT;
     * explicit start still works. A cold boot clears the crash inhibition. */
    if(!radio_last_bad) ble_start(); assert(!ble_wanted);
    plat_ble_start(); ble_poll(); assert(plat_ble_state()==BLE_READY);
    radio_clean_exit(); crashes++; reboot(0); assert(!radio_last_bad);
    open_log.active=1; fm1_boot.p3_rst=4; radio_boot_note(); assert(radio_last_bad);
    open_log.active=1; reboot(1); assert(!radio_last_bad);
    start_fail=1; plat_ble_start(); ble_poll(); assert(plat_ble_state()==BLE_FAILED && !ble_started);
    puts("open peripheral seam: OFF/STARTING/READY/FAILED, linked/subscribed, off/on, queues, crash gate, clean reset and retained log: PASS");
    return 0;
}
