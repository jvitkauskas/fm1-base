/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
/* BLE-MIDI 13-bit millisecond timestamps, packet-local running status, and
 * streaming SysEx. No heap; MIDI realtime does not disturb a pending message. */
#include "ble_midi.h"
static unsigned msg_size(uint8_t status)
{
    if(status<0x80) return 0;
    if(status<0xf0) return (status&0xe0)==0xc0 ? 2u : 3u;
    if(status==0xf1 || status==0xf3) return 2;
    if(status==0xf2) return 3;
    if(status==0xf6 || status>=0xf8) return 1;
    return 0;
}
static uint16_t stamp(uint16_t *high,uint8_t *last,uint8_t low,int *seen)
{
    low&=127;
    if(*seen && low<*last) *high=(uint16_t)((*high+128u)&0x1f80u);
    *last=low; *seen=1; return (uint16_t)(*high|low);
}
static int decode(ble_midi_decoder_t *s,const uint8_t *p,unsigned n,ble_midi_byte_fn emit,void *arg)
{
    unsigned i=1,need=0;
    uint8_t running=0,last=0;
    uint16_t high,ts=0;
    int seen=0,boundary=1;
    if(n<2 || (p[0]&0xc0u)!=0x80u) return 0;
    high=(uint16_t)((p[0]&63u)<<7);
    while(i<n) {
        uint8_t b=p[i++];
        if(s->sysex) {
            if(b&0x80u) {
                ts=stamp(&high,&last,b,&seen);
                if(i==n) return 0;
                b=p[i++];
                if(b!=0xf7 && b<0xf8) return 0;
            } else if(!seen) ts=high;
            if(emit) emit(b,ts,arg);
            if(b==0xf7) { s->sysex=0; boundary=1; running=0; }
            continue;
        }
        if(boundary || (need && (b&0x80u))) {
            if(!(b&0x80u) && (!boundary || !running)) return 0;
            if(b&0x80u) {
                ts=stamp(&high,&last,b,&seen);
                if(i==n) return 0;
                b=p[i++];
            }
            boundary=0;
        }
        if(b>=0xf8) {
            if(emit) emit(b,ts,arg);
            if(!need) boundary=1;
            continue;
        }
        if(b==0xf0) {
            if(need) return 0;
            s->sysex=1; running=0;
            if(emit) emit(b,ts,arg);
            continue;
        }
        if(b&0x80u) {
            unsigned size=msg_size(b);
            if(!size || need) return 0;
            running=b<0xf0 ? b : 0; need=size-1u;
            if(emit) emit(b,ts,arg);
            if(!need) boundary=1;
        } else {
            if(!need) {
                if(!running) return 0;
                need=msg_size(running)-1u;
                if(emit) emit(running,ts,arg);
            }
            if(emit) emit(b,ts,arg);
            if(!--need) boundary=1;
        }
    }
    return need==0;
}
int ble_midi_decode(ble_midi_decoder_t *s,const uint8_t *p,unsigned n,ble_midi_byte_fn emit,void *arg)
{
    ble_midi_decoder_t copy=*s;
    if(!decode(&copy,p,n,0,0)) return 0;
    return decode(s,p,n,emit,arg);
}
int ble_midi_encode_usb(uint32_t packet,uint32_t ms,uint8_t *p,unsigned cap)
{
    static const uint8_t len[16]={0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};
    unsigned cin=packet&15u,n=len[cin],i,at=1;
    int sysex=cin==4 || cin==6 || cin==7 || (cin==5 && ((packet>>8)&255u)==0xf7);
    uint8_t first=(uint8_t)(packet>>8),low=(uint8_t)(0x80u|(ms&127u));
    if(!n || cap<7) return 0;
    p[0]=(uint8_t)(0x80u|((ms>>7)&63u));
    if(!sysex || first==0xf0) p[at++]=low;
    for(i=0;i<n;i++) {
        uint8_t b=(uint8_t)(packet>>(8u*(i+1u)));
        if(sysex && b==0xf7) p[at++]=low;
        p[at++]=b;
    }
    return (int)at;
}
void ble_midi_usb_byte(ble_midi_usb_t *s,uint8_t b,uint16_t ts,ble_midi_usb_fn emit,void *arg)
{
    uint32_t packet; unsigned i;
    if(b>=0xf8) { emit(15u|((uint32_t)b<<8),ts,arg); return; }
    if(b==0xf0) { s->sysex=1; s->sx_n=0; s->n=0; s->status=0; }
    if(s->sysex) {
        s->sx[s->sx_n++]=b;
        if(s->sx_n==3 || b==0xf7) {
            packet=b==0xf7 ? 4u+s->sx_n : 4u;
            for(i=0;i<s->sx_n;i++) packet|=(uint32_t)s->sx[i]<<(8u*(i+1u));
            emit(packet,ts,arg); s->sx_n=0;
        }
        if(b==0xf7) s->sysex=0;
        return;
    }
    if(b&0x80u) { s->status=b; s->need=(uint8_t)msg_size(b); s->n=0; }
    if(!s->need) return;
    s->bytes[s->n++]=b;
    if(s->n==s->need) {
        packet=s->status<0xf0 ? s->status>>4 : s->need==3 ? 3u : s->need==2 ? 2u : 5u;
        for(i=0;i<s->n;i++) packet|=(uint32_t)s->bytes[i]<<(8u*(i+1u));
        emit(packet,ts,arg); s->n=0; s->need=0;
    }
}
