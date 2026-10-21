/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
/* Actual pinned NimBLE, actual HCI/controller/LL objects, simulated RF slots.
 * No socket, BlueZ, USB, hardware register, device, or network access. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_s2_sim.h"
#include "../open_ble/ble_host.h"
static uint8_t l2[512],att[512];
static unsigned l2_n,l2_want,att_n,att_count,control_count;
static uint16_t peer_event_n;
static void control(const uint8_t *p,unsigned n);
static unsigned l2cap_updates;
extern uint16_t ble_hs_hci_avail_pkts;
static void consume_tx(unsigned slot)
{
    unsigned id=(unsigned)sim_slot[slot],n=ble_ll_tx_len(id),llid=ble_ll_tx_llid(id);
    const uint8_t *p=ble_ll_tx_data(id);
    if(llid==3) control_count++;
    else {
        if(llid==2) { l2_n=0; assert(n>=4); l2_want=ble_get16(p)+4u; assert(l2_want<=sizeof l2); }
        assert(l2_n+n<=sizeof l2); memcpy(l2+l2_n,p,n); l2_n+=n;
        if(l2_n==l2_want) {
            if(ble_get16(l2+2)==4) { att_n=l2_n-4u; memcpy(att,l2+4,att_n); att_count++; }
            if(ble_get16(l2+2)==5 && l2_n==16 && l2[4]==0x12) {
                uint8_t rsp[10]={6,0,5,0,0x13,0,2,0,0,0};
                uint8_t update[12]={0,1,0,0,0,0,0,0,0,0,0,0};
                rsp[5]=l2[5];
                assert(ble_ll_rx(1,0xa02,rsp,sim_ticks,peer_event_n));
                memcpy(update+4,l2+10,6); ble_put16(update+10,(uint16_t)(peer_event_n+6u));
                control(update,sizeof update); l2cap_updates++;
            }
            l2_n=l2_want=0;
        }
    }
    sim_ack(slot);
}
static void event(void)
{
    unsigned i,j;
    sim_advance_us((uint32_t)ble_link.conn.interval*1250u);
    assert(ble_ll_rx(1,1,0,sim_ticks,peer_event_n));
    ble_port_refill();
    for(i=0;i<16;i++) {
        int any=0;
        for(j=0;j<2;j++) if(sim_slot[j]>=0) { consume_tx(j); any=1; }
        if(!any) break;
    }
    ble_ll_event(peer_event_n++,sim_ticks);
    ble_link.rx_seen=ble_link.valid_seen=0;
    sim_advance_us(0); ble_host_worker();
}
static void control(const uint8_t *p,unsigned n)
{
    assert(ble_ll_rx(1,(uint16_t)((n<<8)|3u),p,sim_ticks,peer_event_n)); ble_ll_worker();
}
static void request(const uint8_t *p,unsigned n,int expect_response)
{
    uint8_t b[256]; unsigned off=0,before=att_count,i;
    ble_put16(b,(uint16_t)n); ble_put16(b+2,4); memcpy(b+4,p,n); n+=4;
    while(off<n) {
        unsigned take=n-off>27u ? 27u : n-off;
        assert(ble_ll_rx(1,(uint16_t)((take<<8)|(off ? 1u : 2u)),b+off,sim_ticks,peer_event_n));
        off+=take;
    }
    sim_advance_us(0); ble_host_worker();
    for(i=0;i<10 && att_count==before;i++) event();
    if(expect_response) assert(att_count>before);
}
static void connected(void)
{
    ble_conn_t c=sim_connection(); unsigned i;
    peer_event_n=0; l2_n=l2_want=0; sim_slot[0]=sim_slot[1]=-1;
    ble_ll_create(&c,sim_ticks); sim_advance_us(0); ble_host_worker();
    control((uint8_t[9]){8,0xff,1,2,3,4,5,6,7},9);
    control((uint8_t[6]){12,6,1,0,2,0},6);
    for(i=0;i<3;i++) event();
    { ble_host_diag_t d; ble_host_get_diag(&d); assert(d.connection==BLE_HANDLE && d.synced && !d.resets); }
}
int main(void)
{
    static const uint8_t pub[6]={0x58,0x65,0x67,0x8b,0x40,0x62};
    static const uint8_t uuid[16]={0x00,0xc7,0xc4,0x4e,0xe3,0x6c,0x51,0xa7,0x33,0x4b,0xe8,0xed,0x5a,0x0e,0xb8,0x03};
    ble_host_diag_t d; uint8_t query[256]; uint16_t start,end,value,cccd; unsigned i,cycle;
    sim_reset(); sim_executor_enabled=1; ble_init(pub,0); ble_host_start();
    for(i=0;i<5;i++) { ble_host_worker(); sim_advance_us(1000); }
    ble_host_get_diag(&d);
    assert(d.synced && d.running && !d.error && !d.resets && sim_starts==1);
    assert(sim_advertising.data_len==21 && !memcmp(sim_advertising.data+5,uuid,16));
    assert(sim_advertising.scan_len==15 && !memcmp(sim_advertising.scan+2,"FM-1 Open BLE",13));
    assert(!ble_host_linked() && !ble_host_connected() && !ble_host_midi_room());
    assert(!ble_host_pause_advertising());
    unsigned before_pause=sim_starts;
    sim_advance_us(200000); ble_host_worker(); assert(sim_starts==before_pause);
    ble_host_resume_advertising(); sim_advance_us(101000); ble_host_worker();
    assert(sim_starts==before_pause+1);
    connected();
    assert(ble_host_linked() && !ble_host_connected() && !ble_host_midi_room());
    assert(ble_host_pause_advertising()==-8);
    request((uint8_t[]){2,200,0},3,1); assert(att[0]==3 && ble_get16(att+1)==200);
    query[0]=6; ble_put16(query+1,1); ble_put16(query+3,0xffff); ble_put16(query+5,0x2800); memcpy(query+7,uuid,16);
    request(query,23,1); assert(att[0]==7 && att_n==5); start=ble_get16(att+1); end=ble_get16(att+3);
    query[0]=8; ble_put16(query+1,start); ble_put16(query+3,end); ble_put16(query+5,0x2803);
    request(query,7,1); assert(att[0]==9 && att[1]==21 && (att[4]&0x16)==0x16); value=ble_get16(att+5);
    query[0]=4; ble_put16(query+1,(uint16_t)(value+1)); ble_put16(query+3,end);
    request(query,5,1); assert(att[0]==5 && att[1]==1 && ble_get16(att+4)==0x2902); cccd=ble_get16(att+2);
    query[0]=10; ble_put16(query+1,value); request(query,3,1); assert(att[0]==11 && att_n==1);
    query[0]=18; ble_put16(query+1,cccd); query[3]=1; query[4]=0;
    request(query,5,1); assert(att[0]==19); ble_host_get_diag(&d); assert(d.subscribed && d.mtu==200);
    query[0]=0x52; ble_put16(query+1,value); memcpy(query+3,(uint8_t[]){0x80,0x80,0x90,60,100},5);
    request(query,8,0);
    { uint32_t pkt,ts; assert(ble_host_midi_take(&pkt,&ts) && pkt==0x643c9009 && !ble_host_midi_take(&pkt,&ts)); }
    /* Fragmented 70-octet L2CAP PDU: the same three-fragment envelope that
     * S3's SC public key will use, carrying legal MIDI in S2 instead of SMP. */
    query[0]=0x52; ble_put16(query+1,value); query[3]=0x80; query[4]=0x80; query[5]=0x90;
    for(i=0;i<30;i++) { query[6+2*i]=(uint8_t)(32+i); query[7+2*i]=100; }
    request(query,66,0);
    { uint32_t pkt,ts; for(i=0;i<30;i++) assert(ble_host_midi_take(&pkt,&ts)); assert(!ble_host_midi_take(&pkt,&ts)); }
    att_n=0; ble_host_midi_out(0x643c9009,8191); ble_host_worker();
    for(i=0;i<4;i++) event();
    assert(att[0]==0x1b && ble_get16(att+1)==value && att[3]==0xbf && att[4]==0xff && att[5]==0x90);
    assert(ble_hs_hci_avail_pkts==5);
    ble_host_echo(1);
    query[0]=0x52; ble_put16(query+1,value); memcpy(query+3,(uint8_t[]){0x80,0x80,0x90,69,85},5);
    request(query,8,1); assert(att_n==8 && !memcmp(att+3,query+3,5)); ble_host_echo(0);
    { uint32_t pkt,ts; assert(ble_host_midi_take(&pkt,&ts)); }
    /* A subscribed, full TX ring reports zero; off clears it and terminates
     * through real NimBLE/HCI, then on resumes advertising without reinit. */
    assert(ble_host_connected() && ble_host_midi_room()==128);
    for(i=0;i<128;i++) ble_host_midi_out(0x643c9009,i);
    assert(!ble_host_midi_room());
    ble_host_midi_out(0x643c9009,0); ble_host_get_diag(&d); assert(d.midi_drop==1);
    for(i=0;i<100 && ble_host_midi_room()!=128;i++) event();
    assert(ble_host_midi_room()==128);
    /* Complete the peripheral L2CAP update before a distinct central update. */
    for(i=0;i<100 && (!l2cap_updates || ble_link.instant_kind);i++) event();
    assert(l2cap_updates==1 && !ble_link.instant_kind);
    { uint8_t update[12]={0,2,3,0,12,0,0,0,100,0,0,0};
      ble_put16(update+10,(uint16_t)(peer_event_n+8)); control(update,sizeof update);
      for(i=0;i<10;i++) event();
      assert(ble_link.conn.interval==12 && ble_link.conn.timeout==100); }
    /* Simulated ten-minute RF run; host is serviced with sparse MIDI. */
    { uint32_t before_ms=ble_port_ms();
      for(i=0;i<40000;i++) event();
      assert(ble_port_ms()-before_ms>=600000u && ble_port_ms()-before_ms<600100u); }
    ble_host_get_diag(&d); assert(d.running && d.subscribed && !d.resets && !d.acl_drop && !d.event_drop && l2cap_updates==1);
    for(cycle=0;cycle<10;cycle++) {
        control((uint8_t[2]){2,0x13},2); event();
        assert(ble_link.state==BLE_LINK_IDLE); sim_advance_us(101000); ble_host_worker();
        assert(ble_hs_hci_avail_pkts==5); connected();
    }
    /* Disable while linked: real NimBLE Disconnect -> LL_TERMINATE_IND ->
     * ack -> Disconnection Complete, with automatic advertising inhibited. */
    ble_host_enable(0); assert(!ble_host_connected() && !ble_host_midi_room());
    for(i=0;i<8 && ble_link.state!=BLE_LINK_IDLE;i++) event();
    assert(ble_link.state==BLE_LINK_IDLE && !ble_host_linked());
    unsigned stopped_starts=sim_starts;
    sim_advance_us(201000); ble_host_worker(); assert(sim_starts==stopped_starts);
    ble_host_enable(1); sim_advance_us(101000); ble_host_worker(); connected();
    /* Out of range: no event or RX interrupt is needed for supervision. */
    sim_advance_us(5001000); ble_host_worker(); assert(ble_link.reason==8 && ble_link.state==BLE_LINK_IDLE);
    ble_host_enable(0);
    unsigned starts=sim_starts;
    sim_advance_us(1000000); ble_host_worker(); assert(sim_starts==starts && !ble_host_midi_room());
    ble_host_enable(1); sim_advance_us(101000); ble_host_worker(); assert(sim_starts==starts+1);
    ble_host_enable(0); /* Stops an active advertisement via real HCI. */
    assert(!ble_host_connected());
    ble_host_get_diag(&d);
    assert(!d.resets && !d.event_drop && !d.acl_drop && !d.heap_fail && d.connection==0xffff);
    printf("NimBLE 1.9.0 integration: startup, GATT discovery/empty read/CCCD, bidirectional MIDI, fragmented ACL, update, 10-minute simulated link, 10 reconnects, backstop timeout: PASS\nheap peak %u; HCI credits %u; MIDI packets in %u out %u; controller control PDUs %u\n",d.heap_peak,ble_hs_hci_avail_pkts,d.midi_in,d.midi_out,control_count);
    return 0;
}
