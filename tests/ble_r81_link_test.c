/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
/* Specification r8.1 R3 (missed-event step) and R4 (connection priority): every vector of R3.2, every row and
 * vector of R4, and R4 through the link layer's event close on the S2 simulated radio. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_s2_sim.h"

static void host(uint8_t type,const uint8_t *p,uint16_t n) { (void)type; (void)p; (void)n; }

/* ------------------------------------------------------------------------------------------------- R3 --- */
typedef struct { int fine; uint32_t raw; unsigned phase; int applied,next; unsigned drift; } step_t;
static void missed_steps(void)
{
    static const step_t v[]={
        {0,10,525,1,-100,0},{-100,10,525,1,-200,0},{-500,10,525,1,-600,1},{-600,10,525,1,-700,0},
        {100,10,525,1,0,1},{400,10,525,1,300,0},{-1100,10,525,1,-575,1},{-1200,10,525,1,-675,1},
        {-1990,10,525,1,-1465,1},{2047,10,525,1,1947,0},{0,1,525,1,-100,0},{-1,1,525,1,-101,0},
        {-500,1,525,0,0,0},{-500,2,525,1,-600,1},{5,0,525,0,0,0},
        /* other phases */
        {0,10,500,1,-100,0},{-1600,10,0,1,-1075,1},{-2048,20,10000,0,0,0}};
    unsigned i;
    for(i=0;i<sizeof v/sizeof v[0];i++) {
        int next=12345; unsigned drift=777;
        int applied=ble_missed_step(v[i].fine,v[i].raw,v[i].phase,&next,&drift);
        assert(applied==v[i].applied);
        if(applied) assert(next==v[i].next && drift==v[i].drift);
        else assert(next==12345 && drift==777);            /* not applied: nothing written */
    }
    /* The A_FINE word the event close writes for a step (fm1_ble.h): low 16 bits, bit 15 forced, sign-extended. */
    { static const struct { int v; uint32_t word; } w[]={{300,0xFFFF812Cu},{-100,0xFFFFFF9Cu},{-1465,0xFFFFFA47u},{1947,0xFFFF879Bu}};
      for(i=0;i<4;i++) assert((uint32_t)(int32_t)(int16_t)((uint16_t)w[i].v|0x8000u)==w[i].word); }
    printf("R3: %u missed-step vectors and 4 A_FINE words: PASS\n",(unsigned)(sizeof v/sizeof v[0]));
}

/* ------------------------------------------------------------------------------------------------- R4 --- */
static void priority_table(void)
{
    unsigned n;
    /* the vectors */
    assert(ble_link_priority(1,0,0,0)==30);
    assert(ble_link_priority(0,1,0,0)==30);
    assert(ble_link_priority(0,0,0,4)==30);
    assert(ble_link_priority(0,0,0,3)==28);
    assert(ble_link_priority(0,0,1,0)==28);
    assert(ble_link_priority(0,0,0,1)==28);
    assert(ble_link_priority(0,0,0,0)==19);
    /* each row, first match wins */
    for(n=0;n<300;n++) {
        unsigned a;
        for(a=0;a<2;a++) {
            assert(ble_link_priority(1,0,(int)a,n)==30 && ble_link_priority(0,1,(int)a,n)==30);
            assert(ble_link_priority(0,0,(int)a,n)==(n>3 ? 30 : a || n>=1 ? 28 : 19));
        }
    }
    puts("R4: the 7 vectors and the table's rows: PASS");
}
static uint32_t anchor0,interval_ticks;
static void drain(void)
{
    uint8_t p[80],type; uint16_t n;
    for(unsigned i=0;i<32;i++) if(!ble_ll_poll(&type,p,&n) && !ble_link.created_pending) break;
}
static void work(void) { for(unsigned i=0;i<4;i++) ble_ll_worker(); }
static uint32_t at(uint16_t event) { return anchor0+event*interval_ticks; }
static void packet(uint16_t event,const uint8_t *p,uint8_t n)
{
    sim_ticks=at(event)+(80u+8u*n)*24u;
    assert(ble_ll_rx(1,(uint16_t)((uint16_t)n<<8)|(n ? 3u : 1u),p,sim_ticks,event));
    work();
}
static uint8_t close_event(uint16_t event)
{
    sim_ticks=at(event)+1000u*24u; ble_ll_event(event,sim_ticks);
    ble_link.rx_seen=ble_link.valid_seen=0; work(); drain();
    assert(ble_link.state==BLE_LINK_UP);
    return ble_link.priority;
}
static void priority_link(void)
{
    static const uint8_t pub[6]={1,2,3,4,5,6};
    ble_conn_t c=sim_connection();
    uint16_t e=0; unsigned miss;
    sim_reset(); ble_init(pub,host);
    assert(ble_ll_create(&c,sim_ticks)); drain();
    anchor0=ble_link.time.first; interval_ticks=(uint32_t)c.interval*30000u;
    assert(ble_link.priority==28);                                   /* connection set-up */
    /* idle, valid: 19 */
    packet(e,0,0); assert(close_event(e)==19); e++;
    /* n = 1, 2, 3: 28; n = 4, 5: 30; valid again: 19 */
    for(miss=1;miss<=5;miss++) { assert(close_event(e)==(miss>3 ? 30 : 28) && ble_link.invalid_events==miss); e++; }
    packet(e,0,0); assert(close_event(e)==19 && !ble_link.invalid_events); e++;
    /* ACL data waiting (queued, then sent and not yet acknowledged): 28; acknowledged: 19 */
    { static const uint8_t acl[8]={0x50,0x20,4,0,0,0,0x04,0};
      assert(!ble_ll_acl(acl,sizeof acl)); }
    packet(e,0,0); assert(close_event(e)==28); e++;
    assert(sim_slot[0]>=0); sim_ack(0);
    packet(e,0,0); assert(close_event(e)==19); e++;
    /* a control PDU waiting: the reply to the peer's LL_FEATURE_REQ: 30 */
    { uint8_t req[9]={8,0,0,0,0,0,0,0,0};
      packet(e,req,sizeof req); }
    assert(close_event(e)==30); e++;
    while(sim_slot[0]>=0 || sim_slot[1]>=0) { sim_ack(sim_slot[0]>=0 ? 0 : 1); }
    packet(e,0,0); assert(close_event(e)==19); e++;
    /* an instant pending (LL_CHANNEL_MAP_IND far ahead), queues empty, n = 0: 30 */
    { uint8_t ind[8]={1,0xff,0xff,0xff,0xff,0x1f,0,0};
      ble_put16(ind+6,(uint16_t)(e+100u)); packet(e,ind,sizeof ind); }
    assert(ble_link.instant_kind==2);
    assert(close_event(e)==30); e++;
    packet(e,0,0); assert(close_event(e)==30); e++;
    puts("R4: through the event close: set-up 28, idle 19, misses 28/28/28/30/30, ACL 28, control 30, instant 30: PASS");
}
int main(void)
{
    missed_steps();
    priority_table();
    priority_link();
    return 0;
}
