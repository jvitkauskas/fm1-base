/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* The real fm1-base object store over a byte-cuttable NOR, with the actual
 * open seed and bond adapters. No hardware or SDK inputs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_s3_sim.h"
#include "../open_ble/ble_seed.h"
#include "../open_ble/ble_store.h"
#include "../open_ble/ble_bonds.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
extern uint8_t nor[0x100000];
extern int cut,reads_fail,verify_fail;
extern unsigned writes,erases;
static uint8_t baseline[8192];
static void reopen(void) { ble_object_init(); ble_bonds_init(); }
static void fresh(void)
{
    sim_reset(); sim_storage_reset(); memset(nor,255,sizeof nor);
    cut=-1; reads_fail=0; writes=erases=0; reopen();
}
static union ble_store_value value(unsigned peer,unsigned key)
{
    union ble_store_value v; memset(&v,0,sizeof v);
    v.sec.peer_addr.type=0; v.sec.peer_addr.val[0]=(uint8_t)peer;
    v.sec.peer_addr.val[5]=0x21; v.sec.key_size=16;
    v.sec.ltk_present=1; memset(v.sec.ltk,(int)key,16); return v;
}
static union ble_store_key keyof(const union ble_store_value *v)
{
    union ble_store_key k; memset(&k,0,sizeof k); k.sec.peer_addr=v->sec.peer_addr; return k;
}
static void boot_seed(void) { ble_object_init(); sim_seed(); }
static void seed_test(void)
{
    ble_seed_diag_t d; uint8_t r[64],a[8],b[8];
    fresh(); boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_FIRST_BOOT && d.persistence==BLE_PERSIST_STORED && d.sequence==1);
    assert(ble_entropy(a,8)); unsigned e=erases;
    boot_seed(); ble_seed_get_diag(&d); assert(d.state==BLE_SEED_CHAINED && d.sequence==2 && erases==e+1);
    assert(ble_entropy(b,8) && memcmp(a,b,8));
    ble_object_seed_load(r); r[5]=1; ble_put16(r+6,256); ble_put32(r+8,0xffffffffu); ble_put32(r+60,ble_crc32(r,60));
    uint32_t ticks; assert(!ble_object_seed_save(r,&ticks)); boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_PROVISIONED && d.credit==256 && d.sequence==0);
    e=erases; sim_flash_idle=0; boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_UNPERSISTED && d.persistence==BLE_PERSIST_NOT_STORED && erases==e);
    sim_flash_idle=1; sim_aes_fault=1; boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_FAILED && !ble_entropy(a,8)); sim_aes_fault=0;
    sim_aes_fault=0; sim_flash_fault=BLE_FLASH_NOT_ISSUED; boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_UNPERSISTED && d.persistence==BLE_PERSIST_NOT_STORED);
    sim_flash_fault=BLE_FLASH_LATE_READY; boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_UNPERSISTED && d.persistence==BLE_PERSIST_UNVERIFIED);
    sim_flash_fault=0; verify_fail=1; boot_seed(); ble_seed_get_diag(&d);
    assert(d.state==BLE_SEED_UNPERSISTED && d.persistence==BLE_PERSIST_UNVERIFIED);
    reads_fail=verify_fail=0;
    puts("open storage: first/chained/provisioned/wrap/unpersisted/FAILED seeds, one erase per boot: PASS");
}
static void bonds_test(void)
{
    union ble_store_value v=value(1,0x55),out; union ble_store_key k=keyof(&v);
    ble_bond_diag_t d; uint8_t seed[64],after[64];
    fresh(); boot_seed(); ble_object_seed_load(seed);
    assert(!ble_bonds_write(1,&v)); unsigned e=erases;
    sim_flash_idle=0; ble_bonds_flush(); assert(erases==e);
    sim_flash_idle=1; ble_bonds_flush(); assert(erases==e+1); reopen();
    assert(!ble_bonds_read(1,&k,&out) && !memcmp(out.sec.ltk,v.sec.ltk,16));
    for(unsigned i=0;i<40;i++) {
        assert(!ble_bonds_delete(1,&k)); assert(!ble_bonds_write(1,&v)); ble_bonds_flush();
    }
    assert(erases==e+1); ble_bonds_get_diag(&d); assert(!d.pending);
    boot_seed(); ble_object_seed_load(after); assert(memcmp(seed,after,64)); reopen();
    assert(!ble_bonds_read(1,&k,&out));
    v.sec.peer_addr.type=1; v.sec.peer_addr.val[5]=0x40;
    assert(ble_bonds_write(1,&v)==BLE_HS_EINVAL);
    v=value(2,0x66); assert(!ble_bonds_write(1,&v)); ble_bonds_flush(); reopen();
    union ble_store_key k2=keyof(&v); assert(!ble_bonds_read(1,&k2,&out));
    assert(!ble_bonds_read(1,&k,&out));
    v.sec.ltk[0]=0x77; assert(!ble_bonds_write(1,&v));
    sim_flash_fault=BLE_FLASH_NOT_ISSUED; ble_bonds_flush(); ble_bonds_get_diag(&d); assert(d.pending && d.failures);
    sim_flash_fault=0; ble_bonds_flush(); reopen(); assert(!ble_bonds_read(1,&k2,&out) && out.sec.ltk[0]==0x77);
    puts("open storage: deferred identity bonds, two peers, 40 net no-ops, boot preserves bonds, RPA refusal: PASS");
}
static void cache_version_test(void)
{
    union ble_store_value v=value(1,0x55),out;
    union ble_store_key k=keyof(&v);
    uint8_t records[BLE_BOND_SLOTS*128u]; uint32_t version;
    fresh(); boot_seed(); assert(!ble_bonds_write(1,&v)); ble_bonds_flush();
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && version==BLE_GATT_DB_VERSION);
    /* Exact old v1 layout: reserved cache bytes were all zero. No keys or
     * subscriptions are discarded; inner and outer CRCs remain valid. */
    for(unsigned i=0;i<BLE_BOND_SLOTS;i++) {
        uint8_t *r=records+i*128u; assert(!ble_object_bond_load(i,r));
        if(!memcmp(r,"FLBD",4)) { memset(r+12+68,0,4); ble_put32(r+124,ble_crc32(r,124)); }
    }
    assert(!ble_object_bonds_save(records)); reopen();
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && !version);
    assert(!ble_bonds_read(1,&k,&out) && !memcmp(v.sec.ltk,out.sec.ltk,16));
    assert(!ble_bonds_write(1,&v));
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && !version);
    assert(!ble_bonds_db_confirm(0,v.sec.peer_addr.val,BLE_GATT_DB_VERSION));
    unsigned e=erases; sim_flash_idle=0; ble_bonds_flush(); assert(erases==e);
    sim_flash_idle=1; sim_flash_fault=BLE_FLASH_NOT_ISSUED; ble_bonds_flush();
    sim_flash_fault=0; reopen();
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && !version);
    assert(!ble_bonds_db_confirm(0,v.sec.peer_addr.val,BLE_GATT_DB_VERSION));
    ble_bonds_flush(); reopen();
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && version==BLE_GATT_DB_VERSION);
    e=erases; assert(!ble_bonds_db_confirm(0,v.sec.peer_addr.val,BLE_GATT_DB_VERSION));
    ble_bonds_flush(); assert(erases==e);
    union ble_store_value other=value(2,0x66); assert(!ble_bonds_write(1,&other));
    assert(!ble_bonds_db_confirm(0,other.sec.peer_addr.val,0));
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && version==BLE_GATT_DB_VERSION);
    assert(!ble_bonds_delete(1,&k)); assert(!ble_bonds_write(1,&v));
    assert(!ble_bonds_db_version(0,v.sec.peer_addr.val,&version) && version==BLE_GATT_DB_VERSION);
    assert(!ble_bonds_db_version(0,other.sec.peer_addr.val,&version) && !version);
    puts("R3 cache store: old v1 migration, key rewrite preserves stale generation, no linked flash, failed save retries, independent peers, confirmation/re-pair/no-op: PASS");
}
static void cuts_test(void)
{
    union ble_store_value v=value(1,0x55),out; union ble_store_key k=keyof(&v);
    uint8_t previous[64],next[64],loaded[64]; uint32_t ticks;
    fresh(); boot_seed(); assert(!ble_bonds_write(1,&v)); ble_bonds_flush();
    ble_object_seed_load(previous); memcpy(next,previous,64); next[12]^=0x80;
    ble_put32(next+8,ble_get32(previous+8)+1u); ble_put32(next+60,ble_crc32(next,60));
    memcpy(baseline,nor+0xde000u,8192);
    /* Every byte of erase, payload and commit header can be the last byte. */
    for(unsigned n=0;n<=4096+BLE_OBJECT_SIZE+32;n++) {
        memcpy(nor+0xde000u,baseline,8192); cut=-1; reopen(); cut=(int)n;
        (void)ble_object_seed_save(next,&ticks); cut=-1; reopen();
        ble_object_seed_load(loaded);
        assert(!memcmp(loaded,previous,64) || !memcmp(loaded,next,64));
        assert(!ble_bonds_read(1,&k,&out) && out.sec.ltk[0]==0x55);
        if(n==4096+BLE_OBJECT_SIZE+32) assert(!memcmp(loaded,next,64));
    }
    /* Replacement of bond net state also preserves the durable seed. */
    memcpy(nor+0xde000u,baseline,8192); cut=-1; reopen();
    for(unsigned n=0;n<=4096+BLE_OBJECT_SIZE+32;n++) {
        memcpy(nor+0xde000u,baseline,8192); cut=-1; reopen();
        v.sec.ltk[0]=0x66; assert(!ble_bonds_write(1,&v)); cut=(int)n; ble_bonds_flush();
        cut=-1; reopen(); ble_object_seed_load(loaded); assert(!memcmp(previous,loaded,64));
        assert(!ble_bonds_read(1,&k,&out) && (out.sec.ltk[0]==0x55 || out.sec.ltk[0]==0x66));
    }
    /* Both sectors' loader-marker tails stay erased. */
    for(unsigned i=0xf00;i<0x1000;i++) assert(nor[0xde000u+i]==255 && nor[0xdf000u+i]==255);
    puts("open storage: 10,474 byte-cut cases (seed + bond), old/new whole, no compaction loss, loader tails blank: PASS");
}
int main(void) { seed_test(); bonds_test(); cache_version_test(); cuts_test(); return 0; }
