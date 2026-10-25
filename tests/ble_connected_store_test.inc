/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* Included by the actual A/B store suite, under both diagnostic profiles. */
extern int sim_connected_store,read_cut;
extern void (*sim_store_yield)(void);
static void live(void) { sim_connected_store=1; ble_link.state=BLE_LINK_UP; }
static void wait_ms(unsigned ms) { sim_ticks+=ms*24000u; (void)ble_port_ms(); }
static void changed_during_write(void)
{
    union ble_store_value v=value(1,0x77);
    sim_store_yield=0;
    assert(!ble_bonds_write(1,&v));
    ble_bonds_poll(1); /* no recursive transaction */
}
static void connected_store_test(void)
{
    union ble_store_value v=value(1,0x55),out,cccd;
    union ble_store_key k=keyof(&v),ck;
    ble_bond_diag_t d;
    /* Pair -> power cut before, at, and after the key distribution/deadline.
     * Old keys remain until commit; no algorithm promises pre-commit power. */
    for(unsigned ms=0;ms<=1000;ms++) {
        fresh(); live(); assert(!ble_bonds_write(1,&v));
        wait_ms(ms); ble_bonds_poll(ms>=100); /* SMP has finished by 100 ms */
        unsigned saved=erases; reopen();
        int rc=ble_bonds_read(1,&k,&out);
        assert((ms<250 && rc==BLE_HS_ENOENT && !saved) ||
               (ms>=250 && !rc && saved==1 && out.sec.ltk[0]==0x55));
    }
    fresh(); live(); assert(!ble_bonds_write(1,&v));
    wait_ms(1000); ble_bonds_poll(0); assert(!erases); /* still distributing */
    ble_bonds_poll(1); assert(erases==1 && ble_link.state==BLE_LINK_UP);
    /* CCCDs have a five second fixed deadline, never postponed by chatter. */
    memset(&cccd,0,sizeof cccd); cccd.cccd.peer_addr=v.sec.peer_addr;
    cccd.cccd.chr_val_handle=15; cccd.cccd.flags=1;
    memset(&ck,0,sizeof ck); ck.cccd.peer_addr=v.sec.peer_addr; ck.cccd.chr_val_handle=15;
    assert(!ble_bonds_write(3,&cccd));
    for(unsigned i=0;i<49;i++) {
        wait_ms(100); cccd.cccd.value_changed^=1;
        assert(!ble_bonds_write(3,&cccd)); ble_bonds_poll(1); assert(erases==1);
    }
    wait_ms(100); ble_bonds_poll(1); assert(erases==2);
    reopen(); assert(!ble_bonds_read(3,&ck,&out) && out.cccd.flags==1);
    /* Security changes promote an outstanding lower-priority save. */
    cccd.cccd.flags=0; assert(!ble_bonds_write(3,&cccd)); wait_ms(100);
    v.sec.ltk[0]=0x66; assert(!ble_bonds_write(1,&v)); wait_ms(249); ble_bonds_poll(1); assert(erases==2);
    wait_ms(1); ble_bonds_poll(1); assert(erases==3);
    /* Mutation during the preemptible wait must remain pending after readback. */
    v.sec.ltk[0]=0x88; assert(!ble_bonds_write(1,&v)); sim_store_yield=changed_during_write;
    ble_bonds_flush(); ble_bonds_get_diag(&d); assert(d.pending && erases==4);
    wait_ms(250); ble_bonds_poll(1); assert(erases==5); reopen();
    assert(!ble_bonds_read(1,&k,&out) && out.sec.ltk[0]==0x77);
    /* No busy/raw-flash permission leaks to boot seed or legacy XIP profile. */
    uint8_t seed[64]; uint32_t ticks; ble_object_seed_load(seed);
    assert(ble_object_seed_save(seed,&ticks)==BLE_FLASH_REFUSED);
    sim_connected_store=0; v.sec.ltk[0]=0x99; assert(!ble_bonds_write(1,&v));
    ble_bonds_flush(); assert(erases==5); sim_connected_store=1;
    /* An unreadable current copy must not be mistaken for an empty store. */
    memcpy(baseline,nor+0xde000u,8192); reads_fail=1;
    ble_bonds_flush(); ble_bonds_get_diag(&d); assert(d.pending && erases==5);
    assert(!memcmp(baseline,nor+0xde000u,8192)); reads_fail=0;
    for(int point=0;point<3;point++) { /* either header, or winning payload */
        read_cut=point; ble_bonds_flush(); read_cut=-1;
        ble_bonds_get_diag(&d); assert(d.pending && erases==5);
        assert(!memcmp(baseline,nor+0xde000u,8192));
    }
    sim_flash_fault=BLE_FLASH_NOT_ISSUED; ble_bonds_flush(); ble_bonds_get_diag(&d); assert(d.pending);
    sim_flash_fault=0; wait_ms(1000); ble_bonds_poll(1); reopen();
    assert(!ble_bonds_read(1,&k,&out) && out.sec.ltk[0]==0x99);
    /* Every interrupted erase/payload/header byte of a connected replacement.
     * Both banks alternate, and the untouched current copy stays byte exact. */
    for(unsigned bank=0;bank<2;bank++) {
        fresh(); assert(!ble_bonds_write(1,&v)); ble_bonds_flush();
        if(bank) { v.sec.ltk[0]^=1; assert(!ble_bonds_write(1,&v)); ble_bonds_flush(); }
        memcpy(baseline,nor+0xde000u,8192);
        uint8_t old=v.sec.ltk[0];
        for(unsigned n=0;n<=4096+BLE_OBJECT_SIZE+32;n++) {
            memcpy(nor+0xde000u,baseline,8192); cut=-1; reopen(); live();
            v.sec.ltk[0]=0xaa; assert(!ble_bonds_write(1,&v)); cut=(int)n; ble_bonds_flush();
            assert(!memcmp(nor+0xde000u+bank*4096u,baseline+bank*4096u,4096));
            cut=-1; reopen(); assert(!ble_bonds_read(1,&k,&out));
            assert(out.sec.ltk[0]==old || out.sec.ltk[0]==0xaa);
            if(n==4096+BLE_OBJECT_SIZE+32) assert(out.sec.ltk[0]==0xaa);
        }
    }
    sim_connected_store=0;
    puts("R8 connected store: 1001 pairing power points, CCCD/priority, in-flight mutations, retries, both A/B banks at every byte cut: PASS");
}
