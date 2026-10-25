/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* Production platform live-write gate, counters and main-context servicing. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../open_ble/ble_link.h"
#include "../open_ble/ble_store.h"
struct ble_link ble_link;
static uint8_t ble_store_valid=1,flash_ok=1;
static uint32_t ticks,locked,physical,generic,host_calls,input_calls;
static int result,lose_link,risk;
enum { OBJ_BLE=2 };
static uint32_t fm1_ticks(void) { return ticks; }
uint32_t ble_port_lock(void) { uint32_t old=locked; locked=1; return old; }
void ble_port_unlock(uint32_t key) { locked=key; }
static void fm1_wdt_feed(void) { }
int ble_port_storage_idle(void) { return ble_link.state==BLE_LINK_IDLE; }
static int plat_store_load(uint32_t obj,void *p,uint32_t n) { (void)obj; (void)p; (void)n; return -1; }
static int plat_store_save(uint32_t obj,const void *p,uint32_t n) { (void)obj; (void)p; (void)n; generic++; return result; }
static void ble_host_worker(void) { assert(!locked); host_calls++; }
static void app_input_only(void) { assert(!locked); input_calls++; }
static int st_save(uint32_t obj,const void *p,uint32_t n);
#include "platform_store_seam.inc"
static int st_save(uint32_t obj,const void *p,uint32_t n)
{
    (void)p; assert(obj==OBJ_BLE && n==BLE_OBJECT_SIZE && !locked && ble_store_active);
    physical++;
#if FM1B_BLE_CONNECTED_STORE
    fm1_ble_store_masked(2u*24u); fm1_ble_store_operation(1,500000); fm1_ble_store_operation(0,10000);
    for(unsigned i=0;i<1120;i++) {
        ticks+=500u*24u;
        ble_link.time.sup=ticks+(risk ? 100u : 100000u)*24u;
        ble_store_sample(); fm1_ble_flash_service();
    }
#endif
    if(lose_link) { ble_link.state=BLE_LINK_IDLE; ble_link.diag.missed_events+=2; }
    return result;
}
int main(void)
{
    uint8_t payload[BLE_OBJECT_SIZE]={0}; ble_store_diag_t d;
    assert(ble_port_storage_idle() && ble_port_bond_storage_ready());
    assert(!ble_port_object_save(payload,sizeof payload) && generic==1 && !physical);
    ble_link.state=BLE_LINK_UP; ble_link.epoch=2; ble_link.conn.interval=6;
    ble_link.time.sup=ticks+100000u*24u;
#if FM1B_BLE_CONNECTED_STORE
    assert(ble_port_bond_storage_ready());
    assert(!ble_port_object_save(payload,sizeof payload));
    assert(physical==1 && generic==1 && !ble_store_active && !locked && host_calls==1120 && input_calls==1120);
    ble_port_store_diag(&d);
    assert(d.connected_writes==1 && d.save_us_last==560000 && d.save_us_max==560000 && d.stall_us_max==2);
    assert(d.erase_us_max==500000 && d.program_us_max==10000 && !d.missed_events && !d.sup_risk);
    assert(d.sup_margin_us_min==100000 && !d.link_losses);
    risk=lose_link=1; result=-9;
    assert(ble_port_object_save(payload,sizeof payload)==-9);
    ble_port_store_diag(&d);
    assert(!ble_store_active && !locked && d.connected_writes==1 && d.sup_risk==1 && d.link_losses==1 && d.missed_events==2);
    assert(d.sup_margin_us_min==100);
#else
    (void)d; (void)host_calls; (void)input_calls; (void)risk;
    (void)ble_host_worker; (void)app_input_only;
    assert(!ble_port_bond_storage_ready());
    assert(ble_port_object_save(payload,sizeof payload)==-8 && generic==1 && !physical);
#endif
    flash_ok=0; ble_link.state=BLE_LINK_IDLE;
    assert(ble_port_object_save(payload,sizeof payload)==-2);
    ble_store_valid=0; assert(ble_port_object_save(payload,sizeof payload)==-8);
    puts("R8 production platform: live gate / idle fallback, IRQ state, main MIDI service, busy/stall/miss/risk/loss counters: PASS");
}
