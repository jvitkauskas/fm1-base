/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
/* Pure r4 02 §§3.4,5.4-5.8,6; 03 §6.2 arithmetic. */
#include "ble_link.h"
#include "ble_memory.h"
uint8_t ble_map_count(const uint8_t map[5])
{
    unsigned i,n=0;
    for(i=0;i<37;i++) n+=(map[i/8]>>(i%8))&1u;
    return (uint8_t)n;
}
uint8_t ble_csa1(uint8_t *unmapped,uint8_t hop,const uint8_t map[5])
{
    unsigned i,n=ble_map_count(map),r;
    *unmapped=(uint8_t)((*unmapped+hop)%37u);
    if(map[*unmapped/8]&(1u<<(*unmapped%8))) return *unmapped;
    if(!n) return 0xff;
    r=*unmapped%n;
    for(i=0;i<37;i++) if(map[i/8]&(1u<<(i%8))) { if(!r--) return (uint8_t)i; }
    return 0xff;
}
uint8_t ble_rx_class(uint16_t status,uint8_t n)
{
    unsigned code=status&15u;
    if(code&4u) return 1; /* B, including 13 */
    if(n>BLE_LL_PAYLOAD_MAX) return 3;
    if(code==1) return 0;
    if(code==9) return 2;
    return 3;
}
int ble_conn_params_valid(const ble_conn_t *c)
{
    uint32_t effective=(uint32_t)(c->latency+1u)*c->interval;
    return c->interval>=6 && c->interval<=3200 && c->latency<=499 &&
        c->timeout>=10 && c->timeout<=3200 && (uint32_t)c->timeout*4u>effective &&
        c->win>=1 && c->win<=8 && c->win<c->interval && c->offset<=c->interval && effective<=3435;
}
int ble_connect_decode(ble_conn_t *c,const uint8_t *p,uint8_t n,uint8_t header)
{
    if(n!=34) return 0;
    memset(c,0,sizeof *c);
    memcpy(c->peer,p,6); c->peer_type=(header>>6)&1u;
    p+=12; c->aa=ble_get32(p); c->crc=ble_get16(p+4)|((uint32_t)p[6]<<16);
    c->win=p[7]; c->offset=ble_get16(p+8); c->interval=ble_get16(p+10);
    c->latency=ble_get16(p+12); c->timeout=ble_get16(p+14);
    memcpy(c->map,p+16,5); c->map[4]&=31; c->hop=p[21]&31u; c->sca=p[21]>>5;
    return ble_conn_params_valid(c) && c->hop>=5 && c->hop<=16 &&
        c->aa!=0x8e89bed6u && ble_map_count(c->map)>=2;
}
uint16_t ble_widen(const ble_conn_t *c)
{
    static const uint16_t ppm[]={500,250,150,100,75,50,30,20};
    uint32_t v=(uint32_t)(ppm[c->sca&7u]+BLE_LOCAL_SCA_PPM)*c->interval*1250u*(c->latency+1u);
    return (uint16_t)((v/1000000u+1u)*2u);
}
int ble_instant_valid(uint16_t instant,uint16_t closed)
{
    uint16_t diff=(uint16_t)(instant-1u-closed);
    return diff>=1 && diff<=0x7ffe;
}
void ble_timeline_create(ble_timeline_t *t,const ble_conn_t *c,uint32_t now)
{
    memset(t,0,sizeof *t); t->timeout=c->timeout; t->sup=now+(uint32_t)c->timeout*240000u;
    t->first=now+(uint32_t)(c->offset+1u)*30000u;
    t->est_end=t->first+(uint32_t)(5u*c->interval+c->win)*30000u+24000u;
    t->cutoff=t->first+(uint32_t)6u*c->interval*30000u;
}
void ble_timeline_arm(ble_timeline_t *t,uint32_t instant,uint32_t window,uint16_t timeout)
{
    t->inst_reset=instant; t->win_reset=window; t->new_timeout=timeout; t->win_pending=3;
}
static uint8_t timeline_due(ble_timeline_t *t,uint32_t now)
{
    while(t->win_pending) {
        uint32_t when=(t->win_pending&2u) ? t->inst_reset : t->win_reset;
        if((int32_t)(now-when)<0) break;
        if(t->established && (int32_t)(when-t->sup)>=0) return 8; /* expiry wins ties */
        t->timeout=t->new_timeout;
        t->sup=when+(uint32_t)t->timeout*240000u;
        t->win_pending&=(uint8_t)~((t->win_pending&2u) ? 2u : 1u);
    }
    return 0;
}
uint8_t ble_timeline_rx_delayed(ble_timeline_t *t,uint8_t cls,uint32_t stamp,uint32_t service,int delayed)
{
    uint8_t reason=timeline_due(t,delayed ? service : stamp);
    uint32_t deadline;
    if(reason) return reason;
    if(!t->established && cls<3) {
        if((int32_t)(stamp-t->cutoff)>=0) return 0x3e;
        t->established=1;
    }
    deadline=stamp+(uint32_t)t->timeout*240000u;
    if((cls==0 || cls==2) && (!delayed || (int32_t)(deadline-t->sup)>0)) t->sup=deadline;
    return 0;
}
uint8_t ble_timeline_rx(ble_timeline_t *t,const ble_conn_t *c,uint8_t cls,uint32_t now)
{
    (void)c; return ble_timeline_rx_delayed(t,cls,now,now,0);
}
uint8_t ble_timeline_check(ble_timeline_t *t,const ble_conn_t *c,uint32_t now)
{
    uint8_t reason=timeline_due(t,now); (void)c;
    if(reason) return reason;
    if(!t->established) return (int32_t)(now-t->est_end)>=0 ? 0x3e : 0;
    return (int32_t)(now-t->sup)>=0 ? 8 : 0;
}
/* Missed-event step, specification r8.1 R3.2: the (A_DRIFT, A_FINE) pair that moves the expected anchor 100 us
 * earlier. fine: the current A_FINE, sign-extended; raw: the raw 24-bit A_CNT; phase: P = 625 - TIM_B - TIM_D,
 * computed in unsigned arithmetic by the caller, so a negative P arrives wrapped and is read back as signed.
 * Returns 1 with *next and *drift set, or 0 when the step is not applied (A_DRIFT >= raw A_CNT, or the new A_FINE
 * outside -2048..2047); then neither output is written. */
int ble_missed_step(int fine,uint32_t raw,unsigned phase,int *next,unsigned *drift)
{
    int64_t q=(int64_t)(int32_t)phase+fine,moved=(int64_t)fine-(int64_t)BLE_MISS_STEP_US;
    unsigned d=0;
    if((q>=0 && q<100) || (q>=625 && q<725)) d=1;
    else if(q< -525) { d=1; moved=(int64_t)fine+525; }
    if(d>=raw || moved< -2048 || moved>2047) return 0;
    *next=(int)moved; *drift=d; return 1;
}
/* Connection-event priority, specification r8.1 R4 (first match wins). missed: consecutive events without a
 * valid packet, including the one just ended (0 if it was valid). */
uint8_t ble_link_priority(int instant_pending,int control_waiting,int acl_waiting,unsigned missed)
{
    if(instant_pending || control_waiting || missed>3u) return 30;
    if(acl_waiting || missed>=1u) return 28;
    return 19;
}
