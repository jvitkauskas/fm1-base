/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* Production RAM busy-wait against an interrupt-driven timer/NOR model.
 * This does not claim to model the RF analogue path or physical flash timing. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../open_ble/ble_link.h"
#define RAMINL static inline
#define FM1B_BLE_CONNECTED_STORE 1
static uint32_t now,ready_at,next_event,next_audio,last_service,interval_us;
static unsigned events,audio,services,resets;
static ble_timeline_t timeline;
static ble_conn_t conn;
static jmp_buf reset_jump;
static volatile uint32_t reset_reg;
static volatile uint32_t *reset_register(uint32_t address)
{
    assert(address==0x10000u); resets++; longjmp(reset_jump,1); return &reset_reg;
}
#define REG32(a) (*reset_register(a))
#define T4_CNT now
static uint8_t fl_rdsr(uint8_t command)
{
    assert(command==5); now+=5u*24u;
    /* The CPU executes from SRAM; no event or audio interrupt is masked by
     * erase/program/status transactions, including at late-ready limits. */
    if((int32_t)(now-next_audio)>=0) { audio++; next_audio+=5805u*24u; }
    if((int32_t)(now-next_event)>=0) {
        assert(!ble_timeline_check(&timeline,&conn,now));
        ble_timeline_rx(&timeline,&conn,0,now); events++; next_event+=interval_us*24u;
    }
    return now<ready_at;
}
static void fm1_ble_flash_service(void)
{
    if(services) assert(now-last_service<=1010u*24u);
    last_service=now; services++;
}
#include "flash_wait_seam.inc"
static void reset_model(unsigned interval,unsigned timeout)
{
    now=0; ready_at=0; interval_us=interval; next_event=interval*24u;
    next_audio=5805u*24u; events=audio=services=resets=0; last_service=0;
    memset(&conn,0,sizeof conn); conn.interval=(uint16_t)(interval/1250u); conn.timeout=(uint16_t)timeout;
    ble_timeline_create(&timeline,&conn,0); ble_timeline_rx(&timeline,&conn,0,1);
}
static void operation(unsigned duration,unsigned normal,unsigned extended,int expected)
{
    uint32_t took=0,start=now;
    ready_at=now+duration*24u;
    assert(fl_wait_issued(now,normal,extended,&took)==expected);
    assert(took>=duration && took<=duration+5u);
    assert(now-start<=duration*24u+120u);
}
static void unresolved_busy(void)
{
    ready_at=3000000u*24u;
    if(!setjmp(reset_jump)) { uint32_t took; fl_wait_issued(0,500000,2000000,&took); assert(0); }
    assert(resets==1 && now>=2000000u*24u && events>100);
}
int main(void)
{
    for(unsigned i=0;i<2;i++) {
        unsigned interval=i ? 15000u : 7500u;
        for(unsigned timeout=10;timeout<=600;timeout+=590) {
            reset_model(interval,timeout);
            operation(500000,500000,2000000,0);
            for(unsigned page=0;page<6;page++) operation(10000,10000,50000,0);
            assert(events==560000u/interval && audio==560000u/5805u);
            assert(services>1000 && !resets && !ble_timeline_check(&timeline,&conn,now));
            printf("R8 RAM flash: interval %u us, supervision %u ms, 560 ms total, %u RF events / %u audio blocks serviced, zero blocked events/xruns: PASS\n",interval,timeout*10,events,audio);
            /* Same physical limits with the old global blackout are unsafe.
             * Best-aligned misses floor(T/I); arbitrary phase ceil(T/I). */
            assert(500000u/interval== (i ? 33u : 66u));
            assert(10000u>5805u);
            reset_model(interval,timeout); now=500000u*24u;
            assert(ble_timeline_check(&timeline,&conn,now)==(timeout==10 ? 8 : 0));
        }
        reset_model(interval,10);
        operation(1999000,500000,2000000,-9);
        operation(49000,10000,50000,-9);
        assert(events>100 && !ble_timeline_check(&timeline,&conn,now));
        /* Never execute XIP on unresolved busy; the real RAM routine resets. */
        reset_model(interval,10); unresolved_busy();
    }
    puts("R8 production RAM flash wait: normal/late-ready/unresolved busy, MIDI service bound, blackout counterexample: PASS");
    return 0;
}
