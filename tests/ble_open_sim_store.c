/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
#include <assert.h>
#include <string.h>
#include "ble_s3_sim.h"
#include "../open_ble/ble_seed.h"
#include "../open_ble/ble_store.h"
enum { OBJ_SETTINGS,OBJ_PANEL,OBJ_BLE,OBJ_NOBJ };
#define APP_STORE_MAP {0xfc000u,0xdc000u,0xde000u}
uint8_t nor[0x100000];
int cut=-1,reads_fail,verify_fail,read_cut=-1;
int sim_connected_store;
void (*sim_store_yield)(void);
void (*sim_store_operation)(int erase,uint32_t off,uint32_t n);
int ble_port_bond_storage_ready(void)
{
    return sim_flash_idle && (ble_port_storage_idle() || (sim_connected_store && ble_link.state==BLE_LINK_UP));
}
unsigned writes,erases;
static int st_read(uint32_t off,void *p,uint32_t n)
{
    assert(off>=0xde000u && off+n<=0xe0000u);
    if(reads_fail || !read_cut) return -1;
    if(read_cut>0) read_cut--;
    memcpy(p,nor+off,n); return 0;
}
static int step(void) { if(!cut) return -1; if(cut>0) cut--; return 0; }
static int st_erase(uint32_t off)
{
    assert(off==0xde000u || off==0xdf000u); erases++;
    if(sim_store_yield) sim_store_yield();
    if(sim_store_operation) sim_store_operation(1,off,4096);
    for(unsigned i=0;i<4096;i++) { if(step()) return -50; nor[off+i]=255; }
    return 0;
}
static int st_prog(uint32_t off,const void *p,uint32_t n)
{
    const uint8_t *s=p; writes++;
    if(sim_store_operation) sim_store_operation(0,off,n);
    assert(off>=0xde000u && off+n<=0xe0000u);
    for(unsigned i=0;i<n;i++) { if(step()) return -50; assert((nor[off+i]&s[i])==s[i]); nor[off+i]&=s[i]; }
    return 0;
}
#include "../device/storage.c"
int ble_port_object_load(void *p,uint32_t n) { return st_load(OBJ_BLE,p,n); }
int ble_port_object_save(const void *p,uint32_t n)
{
    if(!ble_port_bond_storage_ready()) return -8;
    if(sim_flash_fault==BLE_FLASH_REFUSED) return -8;
    if(sim_flash_fault==BLE_FLASH_NOT_ISSUED) return -2;
    int rc=st_save(OBJ_BLE,p,n);
    if(sim_flash_mismatch) nor[0xde000u+256u]^=1;
    if(!rc && sim_flash_fault==BLE_FLASH_LATE_READY) return -9;
    if(verify_fail) reads_fail=1;
    return rc;
}

void sim_object_reset(void)
{
    memset(nor,255,sizeof nor); cut=read_cut=-1; reads_fail=verify_fail=0; writes=erases=0; ble_object_init();
}
