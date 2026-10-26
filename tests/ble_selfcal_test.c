/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
/* Radio self-calibration (specification r8.2 C4): the self-calibration block of open_ble/hal/fm1_ble_radio.h and
 * its br_tx/br_analog_dc helpers, cut verbatim by tests/run_open_ble_tests.py, run against a model of the chip.
 * The model keeps the analog registers, the PHY port and the PHY banks C8/D0/D4, and answers the measurements
 * from the values programmed at the moment a measurement starts. It also checks the measurement sequences, the
 * 1 ms peak wait, the loop-back set-up and the RF mode of every measurement. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../open_ble/ble_s1.h"

/* ---- the chip model ---- */
typedef struct {
    int a,c,b,d,K,p_I,p_Q,T_I[4],T_Q[4],KR,q_I,q_Q;
    int flat;            /* TX/RX costs use max(0, |x| - flat): a flat minimum of width 2 x flat + 1 */
    int coupling,offset; /* stage E: M(1) = 37 (vI - TI) + coupling (vQ - TQ) + offset, and the same for M(2) */
    int peak_base;       /* ADC peak = peak_base + gain code (C4: 30) */
} model_t;
static model_t m;
static uint32_t wla[64],mac_31004,now,last_feed,max_feed_gap;
static uint8_t phy[256],bank_c8[256],bank_d0[256],bank_d4[256];
static unsigned mode=99,gain_code=99,measures,dc_measures,phy_resets,comparator_reads;
static unsigned busy_polls=2,busy_left,bw_ready_reads=3,bw_reads_left,bw_config;   /* ~0 = never ready */
static int seq_state,peak_armed;
static uint32_t peak_armed_at;
static struct { uint32_t error,stage,failure_stage; } br_diag;
static uint8_t br_dc[16];

static uint32_t fm1_ticks(void) { return now; }
static void fm1_delay_us(uint32_t us) { now+=24u*us; }
static void fm1_wdt_feed(void)
{
    if(now-last_feed>max_feed_gap) max_feed_gap=now-last_feed;
    last_feed=now;
}
static void br_error(uint32_t code)
{
    if (!br_diag.error) { br_diag.error=code; br_diag.failure_stage=br_diag.stage; }
}
static unsigned bandwidth_code(void)
{
    switch(wla[26]&0x3feu)   /* bit 0 is the start bit */ {
    case 0x1fc: return 0xf9;
    case 0x122: return 0x7f;
    case 0x0a0: return 0x37;
    default: assert(!"bandwidth configuration"); return 0;
    }
}
static uint32_t *reg(uint32_t a)
{
    now+=1;
    if(a==0x31004u) return &mac_31004;
    assert(a>=0x11900u && a<0x11900u+4u*64u && !(a&3u));
    if(a==0x11900u+4u*30u) {                        /* WLA_CON30, read once more after the poll */
        assert(wla[26]&1u);
        wla[30]=0x20u|bandwidth_code()<<8;
    }
    return &wla[(a-0x11900u)/4u];
}
#define BR_R(a) (*reg((uint32_t)(a)))
#define BR_A(n) BR_R(0x11900u+4u*(n))
static void br_field(uint32_t addr,uint32_t mask,uint32_t value)
{
    if (!br_diag.error) BR_R(addr)=(BR_R(addr)&~mask)|(value&mask);
}
static void br_af(unsigned n,uint32_t mask,uint32_t value) { br_field(0x11900u+4u*n,mask,value); }
static void br_phy_write(unsigned r,uint8_t data) { now+=24; if(!br_diag.error) phy[r&255u]=data; }
static uint8_t br_phy_read(unsigned r) { now+=24; return br_diag.error ? 0 : phy[r&255u]; }
static void br_phy_reset(void)
{
    if (br_diag.error) return;
    mac_31004=12; phy[22]=0; phy[21]=0x30; phy[25]=0; phy_resets++;
}
static uint8_t *bank(unsigned b)
{
    assert(b==0xc8 || b==0xd0 || b==0xd4);
    return b==0xc8 ? bank_c8 : b==0xd0 ? bank_d0 : bank_d4;
}
static int imax(int a,int b) { return a>b ? a : b; }
static int dist(int x) { return imax(0,abs(x)-m.flat); }
static int s10(unsigned v) { return (v&512u) ? (int)v-1024 : (int)v; }
static int tx_field(unsigned f)
{
    unsigned packed=f<4 ? bank_d0[97] : bank_d0[98],shift=(f<4 ? f : f-4)*2;
    unsigned v=((unsigned)bank_d0[4+f]<<2)|((packed>>shift)&3u);
    return (f==0 || f==2) ? (int)v : s10(v);
}
static int analog_dc(unsigned q)
{
    unsigned code=(wla[2]>>(q ? 17 : 7))&1023u,mag=code&511u;
    return (code&512u) ? (int)mag : -(int)mag;
}
static int group(void)
{
    static const unsigned g[4][3]={{0,0,127},{1,0,127},{1,3,69},{1,9,14}};
    unsigned a=(wla[3]>>6)&3u,b=(wla[4]>>8)&15u,c=wla[4]&127u;
    for(int i=0;i<4;i++) if(a==g[i][0] && b==g[i][1] && c==g[i][2]) return i;
    assert(!"RX gain group");
    return 0;
}
static long model_m1(int g,int vi,int vq) { return 37L*(vi-m.T_I[g])+(long)m.coupling*(vq-m.T_Q[g])+m.offset; }
static long model_m2(int g,int vi,int vq) { return 37L*(vq-m.T_Q[g])+(long)m.coupling*(vi-m.T_I[g])+m.offset; }
static void put16(unsigned r,int v) { bank_d4[r]=(uint8_t)((unsigned)v>>8); bank_d4[r+1]=(uint8_t)v; }
static void put24(unsigned r,long v)
{
    uint32_t u=(uint32_t)v&0xffffffu;
    assert(v>=-0x800000L && v<0x800000L);
    bank_d4[r]=(uint8_t)(u>>16); bank_d4[r+1]=(uint8_t)(u>>8); bank_d4[r+2]=(uint8_t)u;
}
static void measurement_start(void)
{
    measures++;
    assert(mac_31004==12 && phy[25]==24);            /* loop-back set-up in place */
    assert(bank_d4[0]==62);
    if(mode==2) {                                     /* M(0) */
        int ig=tx_field(0),qg=tx_field(2),k=ig<1023 ? ig-1023 : 1023-qg;
        int lo_i=(int)((wla[7]>>8)&127u),lo_q=(int)(wla[8]&127u);
        int i=20*dist(lo_i-m.a)+15*dist(tx_field(4)-m.b)+9*dist(k-m.K)+7*dist(tx_field(1)-m.p_I);
        int q=20*dist(lo_q-m.c)+15*dist(tx_field(5)-m.d)+7*dist(tx_field(3)-m.p_Q);
        assert(i<32768 && q<32768);
        put16(0x81,i); put16(0x84,q);
    } else if(mode==0) {                              /* M(1), M(2) */
        int g=group();
        assert(gain_code==40);
        dc_measures++;
        put24(0x81,model_m1(g,analog_dc(0),analog_dc(1)));
        put24(0x8a,model_m2(g,analog_dc(0),analog_dc(1)));
    } else if(mode==3) {                              /* M(3) */
        int ig=bank_d4[9],qg=bank_d4[11],k=ig<255 ? ig-255 : 255-qg;
        int i=11*dist(k-m.KR)+6*dist((int8_t)bank_d4[10]-m.q_I),q=5*dist((int8_t)bank_d4[12]-m.q_Q);
        put16(0x81,i); put16(0x84,q);
    } else assert(!"measurement in an unexpected RF mode");
    busy_left=busy_polls;
}
static void br_bank_write(unsigned b,unsigned r,uint8_t value)
{
    now+=72;
    if(br_diag.error) return;
    if(b==0xd4) {
        static const uint8_t seq[4][2]={{21,1},{22,1},{21,3},{22,3}};
        if(r==21 || r==22) {
            if(r==seq[seq_state][0] && value==seq[seq_state][1]) seq_state++;
            else { assert(!seq_state && !"measurement sequence out of order"); }
            if(seq_state==4) { seq_state=0; measurement_start(); }
        } else assert(!seq_state);
        if(r==23) {
            if(value==0x82) { peak_armed=1; peak_armed_at=now; }
            else { assert(value==2 && peak_armed); peak_armed=0; }
        }
    }
    bank(b)[r&255u]=value;
}
static uint8_t br_bank_read(unsigned b,unsigned r)
{
    now+=72;
    if(br_diag.error) return 0;
    if(b==0xd4 && r==0x80) {
        assert(peak_armed && now-peak_armed_at>=24000u);   /* 1 ms after reg 23 = 0x82 */
        return (uint8_t)(m.peak_base+(int)gain_code);
    }
    if(b==0xd4 && r==0x8d) {
        if(busy_left==~0u) return 3;
        if(busy_left) { busy_left--; return 1; }
        return 0;
    }
    return bank(b)[r&255u];
}
static void br_bank_field(unsigned b,unsigned r,uint8_t mask,uint8_t value)
{
    uint8_t old=br_bank_read(b,r);
    br_bank_write(b,r,(uint8_t)((old&~mask)|(value&mask)));
}
static void br_mode(unsigned value) { if(!br_diag.error) mode=value; }
static void br_gain(unsigned code) { if(!br_diag.error) { assert(code<42); gain_code=code; } }
static uint32_t br_comparator(void)
{
    if (br_diag.error) return 0;
    comparator_reads++; now+=24;
    br_af(26,0xf0000000u,0x10000000u);
    assert(((wla[26]>>10)&255u)==0x1e && !((wla[26]>>18)&63u));
    assert(((wla[1]>>8)&15u)==8 && (wla[20]&(1u<<27)));
    if(!(wla[26]&1u)) return 0;
    if((wla[26]&0x3feu)!=bw_config) { bw_config=wla[26]&0x3feu; bw_reads_left=bw_ready_reads; }   /* a new code */
    if(bw_reads_left==~0u) return 0;
    if(bw_reads_left) { bw_reads_left--; return 0; }
    return 0x20u|bandwidth_code()<<8;
}

#define BR_ERR_VCO_DETECTOR 12u   /* defined above the block in the header; br_error_cause names it */
#include "selfcal_helpers.inc"
#include "selfcal_seam.inc"
#include "selfcal_cause.inc"

/* ---- the test ---- */
static void reset_model(const model_t *model)
{
    m=*model;
    memset(wla,0,sizeof wla); memset(phy,0,sizeof phy);
    memset(bank_c8,0x5a,sizeof bank_c8); memset(bank_d0,0x5a,sizeof bank_d0); memset(bank_d4,0x5a,sizeof bank_d4);
    memset(br_dc,0xee,sizeof br_dc); memset(&br_diag,0,sizeof br_diag);
    mac_31004=0; now=last_feed=max_feed_gap=0; mode=gain_code=99;
    measures=dc_measures=phy_resets=comparator_reads=0; busy_polls=2; bw_ready_reads=3;
    seq_state=peak_armed=0;
    wla[26]=0xa0000000u; wla[20]=0x12345678u&~(1u<<27);
}
static uint32_t run(uint8_t *record)
{
    uint32_t start=now;
    bw_config=~0u;
    br_selfcal_run();
    if(!br_diag.error) { memset(record,0xcc,68); br_selfcal_record(record); }
    return now-start;
}
static void hex(const char *text,uint8_t *out)
{
    unsigned n=0;
    while(*text) {
        unsigned v;
        if(*text==' ' || *text=='\n') { text++; continue; }
        assert(sscanf(text,"%2x",&v)==1); out[n++]=(uint8_t)v; text+=2;
    }
    assert(n==68);
}
static int dc_value(const uint8_t *p) { unsigned c=ble_get16(p)&1023u; return (c&512u) ? (int)(c&511u) : -(int)(c&511u); }
/* The record against the expected one of C4: exact except the analog DC values and the tune table (each value
 * within 1) and then the check word, which must still be the record's own. */
static int compare(const uint8_t *got,const uint8_t *want)
{
    int exact=!memcmp(got,want,68);
    assert(!memcmp(got,want,32) && !memcmp(got+40,want+40,8));
    for(int i=0;i<2;i++) assert(abs((int32_t)ble_get32(got+32+4*i)-(int32_t)ble_get32(want+32+4*i))<=1);
    for(int i=0;i<8;i++) assert(abs(dc_value(got+48+2*i)-dc_value(want+48+2*i))<=1);
    assert(ble_get32(got+64)==ble_crc16(0,got,64) && ble_trim_valid(got));
    return exact;
}
/* The stage E requirement checked directly on the model: with the group's final pair (from the tune table),
 * each axis has M = 0, or opposite signs at the value and a neighbour; or, if no value in -510..510 qualifies
 * with the other axis held, -510 when M is positive over the whole range and +510 when negative. */
static int sgn(long v) { return (v>0)-(v<0); }
static void check_null(int g,int vi,int vq)
{
    for(int axis=0;axis<2;axis++) {
        int v=axis ? vq : vi,any=0,pos=1,neg=1,x;
        long (*f)(int,int,int)=axis ? model_m2 : model_m1;
        #define AT(x_) (axis ? f(g,vi,(x_)) : f(g,(x_),vq))
        int ok=!sgn(AT(v)) || (v>-510 && sgn(AT(v))*sgn(AT(v-1))<0) || (v<510 && sgn(AT(v))*sgn(AT(v+1))<0);
        for(x=-510;x<=510;x++) {
            int s=sgn(AT(x));
            if(s<=0) pos=0;
            if(s>=0) neg=0;
            if(!s || (x<510 && s*sgn(AT(x+1))<0)) any=1;
        }
        #undef AT
        if(any) { if(!ok) { printf("group %d axis %d value %d (pair %d %d) is not a null\n",g,axis,v,vi,vq); assert(ok); } }
        else assert((pos && v==-510) || (neg && v==510));
    }
}
static void check_stage_e(const uint8_t *record)
{
    for(int g=0;g<4;g++) check_null(g,dc_value(record+48+4*g),dc_value(record+48+4*g+2));
    assert((int32_t)ble_get32(record+32)==dc_value(record+60) && (int32_t)ble_get32(record+36)==dc_value(record+62));
    assert(analog_dc(0)==dc_value(record+60) && analog_dc(1)==dc_value(record+62));   /* last pair programmed */
}
static const char *const S0_RECORD="ff030000 00000000 f4030000 05000000 0c000000 f9ffffff 3246fbff 03000000"
    " d4feffff 9a010000 fff97f37 fff97f37 7802c800 1e002d02 04030502 2c019a03 81ec0000";
static const model_t S0={50,70,12,-7,11,0,5,{120,-30,260,-300},{-200,45,5,410},-4,3,0,0,0,0,30};
static const model_t S1={93,12,-90,101,-37,-60,0,{-77,15,-5,333},{88,-160,-2,7},9,0,-12,0,0,0,30};
static void vector(const char *name,const model_t *model,const char *expected)
{
    uint8_t got[68],want[68];
    uint32_t ticks;
    reset_model(model); hex(expected,want);
    ticks=run(got);
    assert(!br_diag.error);
    printf("%s: ",name); for(int i=0;i<68;i++) printf("%02x%s",got[i],i%4==3 ? " " : "");
    printf("\n");
    int exact=compare(got,want);
    check_stage_e(got);
    assert(gain_code==18 && mode==3 && (wla[5]&31u)==25 && !(wla[1]&(1u<<15)) && !(wla[20]&(1u<<27)));
    assert(!(wla[26]&1u) && ((wla[8]>>20)&15u)==6 && ((wla[10]>>27)&3u)==1);
    assert(max_feed_gap<24000000u);
    printf("%s: %s; %u measurements (%u in stage E), %u phy resets, model time %u ms: PASS\n",name,
        exact ? "record identical to C4" : "record within the C4 tolerance",measures,dc_measures,phy_resets,
        (unsigned)(ticks/24000u));
}
/* Flat minima: the start value wins when it is among the minima, else the smallest minimum. */
static int best(int start,int target,int flat,int first,int last)
{
    int lo=target-flat;
    if(abs(start-target)<=flat) return start;
    return lo<first ? first : lo>last ? last : lo;   /* with target in range, first..last contains lo or first */
}
static void flat_case(int flat)
{
    model_t t=S0;
    uint8_t got[68];
    int k,kr,i_gain,q_gain,phase_q,phase_i,ri,rq,rpi,rpq;
    t.flat=flat;
    reset_model(&t); run(got);
    assert(!br_diag.error);
    k=best(0,t.K,flat,-127,127); kr=best(0,t.KR,flat,-105,105);
    i_gain=k<0 ? 1023+k : 1023; q_gain=k<0 ? 1023 : 1023-k;
    phase_q=k>=0 ? best(0,t.p_Q,flat,-200,200) : 0; phase_i=k<0 ? best(0,t.p_I,flat,-200,200) : 0;
    ri=kr<0 ? 255+kr : 255; rq=kr<0 ? 255 : 255-kr;
    rpi=kr<0 ? best(0,t.q_I,flat,-50,50) : 0; rpq=kr>=0 ? best(0,t.q_Q,flat,-50,50) : 0;
    assert((int32_t)ble_get32(got)==i_gain && (int32_t)ble_get32(got+8)==q_gain);
    assert((int32_t)ble_get32(got+4)==phase_i && (int32_t)ble_get32(got+12)==phase_q);
    assert((int32_t)ble_get32(got+16)==best(0,t.b,flat,-128,127) && (int32_t)ble_get32(got+20)==best(0,t.d,flat,-128,127));
    assert(got[24]==best(64,t.a,flat,0,127) && got[25]==best(64,t.c,flat,0,127));
    assert(got[26]==ri && got[27]==rq && (int8_t)got[28]==rpi && (int8_t)got[29]==rpq);
    printf("flat minimum %d: LO %u %u, DC %d %d, TX gains %d %d, phases %d %d; RX gains %u %u, phases %d %d: PASS\n",
        flat,got[24],got[25],(int)ble_get32(got+16),(int)ble_get32(got+20),i_gain,q_gain,phase_i,phase_q,
        got[26],got[27],(int8_t)got[28],(int8_t)got[29]);
}
static uint32_t lcg=12345;
static int rnd(int lo,int hi) { lcg=lcg*1103515245u+12345u; return lo+(int)((lcg>>8)%(uint32_t)(hi-lo+1)); }
int main(void)
{
    uint8_t got[68];
    vector("S0",&S0,S0_RECORD);
    vector("S1",&S1,"da030000 c4ffffff ff030000 00000000 a6ffffff 65000000 5d0cfff6 00f40000 4d010000 07000000"
        " fff97f37 fff97f37 4d005802 0f02a000 05000200 4d030702 32b60000");
    flat_case(3); flat_case(6); flat_case(40);
    /* Targets on and beyond the ends of the search ranges. */
    { model_t t=S0; t.a=127; t.c=0; t.b=-128; t.d=200; t.K=-300; t.p_I=-250; t.KR=150; t.q_Q=-90;
      reset_model(&t); run(got); assert(!br_diag.error);
      assert(got[24]==127 && got[25]==0 && (int32_t)ble_get32(got+16)==-128 && (int32_t)ble_get32(got+20)==127);
      assert(ble_get32(got)==1023u-127u && ble_get32(got+8)==1023 && (int32_t)ble_get32(got+4)==-200);
      assert(got[26]==255 && got[27]==255-105 && (int8_t)got[29]==-50);
      puts("targets at and beyond the range ends: PASS"); }
    /* Gain setting with no code above 47: G = 36. */
    { model_t t=S0; t.peak_base=5; reset_model(&t); run(got); assert(!br_diag.error && gain_code==36);
      puts("gain setting without a peak above 47: 36: PASS"); }
    /* Stage E without an exact zero, with coupled axes, and with no null in range (both directions). */
    { model_t t=S0; t.offset=18; reset_model(&t); run(got); assert(!br_diag.error); check_stage_e(got);
      t.coupling=20; reset_model(&t); run(got); assert(!br_diag.error); check_stage_e(got);
      t.coupling=-30; t.offset=-5; reset_model(&t); run(got); assert(!br_diag.error); check_stage_e(got);
      t=S0; t.T_I[0]=600; t.T_Q[0]=-700; t.T_I[3]=-520; t.T_Q[3]=511; reset_model(&t); run(got); assert(!br_diag.error);
      check_stage_e(got);
      assert(dc_value(got+48)==510 && dc_value(got+50)==-510 && dc_value(got+60)==-510 && dc_value(got+62)==510);
      puts("stage E: offset null, coupled axes, no null in range: PASS"); }
    /* Random models: independent axes (with an offset, so a null may lie between two values), then coupled axes.
     * Every result is checked against the condition itself; the counts are stage E's measurements. */
    for(int set=0;set<3;set++) {
        static const int coupling[3]={0,3,18};
        unsigned worst=0,total=0;
        for(int n=0;n<3000;n++) {
            model_t t=S0;
            for(int g=0;g<4;g++) { t.T_I[g]=rnd(-560,560); t.T_Q[g]=rnd(-560,560); }
            t.coupling=rnd(-coupling[set],coupling[set]); t.offset=rnd(-40,40);
            reset_model(&t); run(got); assert(!br_diag.error); check_stage_e(got);
            if(dc_measures>worst) worst=dc_measures;
            total+=dc_measures;
        }
        if(!set) assert(worst<=60);
        printf("stage E: 3000 random models, coupling |c| <= %d: null condition met; measurements for 4 groups: "
            "mean %.1f, at most %u: PASS\n",coupling[set],total/3000.0,worst);
    }
    /* Timeouts: a measurement that never ends, and a bandwidth code that is never ready. */
    { uint32_t ticks;
      reset_model(&S0); busy_polls=~0u; ticks=run(got);
      assert(br_diag.error==BR_ERR_SC_MEASURE && measures==1);
      assert(br_error_cause(br_diag.error) && strlen(br_error_cause(br_diag.error))<=43);
      printf("measurement timeout: error %u \"%s\" after 1 measurement, model time %u ms: PASS\n",
          br_diag.error,br_error_cause(br_diag.error),(unsigned)(ticks/24000u));
      reset_model(&S0); bw_ready_reads=~0u; ticks=run(got);
      assert(br_diag.error==BR_ERR_SC_BANDWIDTH && !measures && mode==99);
      assert(br_error_cause(br_diag.error) && strlen(br_error_cause(br_diag.error))<=43);
      assert(strcmp(br_error_cause(BR_ERR_SC_MEASURE),br_error_cause(BR_ERR_SC_BANDWIDTH)));
      printf("bandwidth timeout: error %u \"%s\" after %u comparator reads, model time %u ms: PASS\n",
          br_diag.error,br_error_cause(br_diag.error),comparator_reads,(unsigned)(ticks/24000u));
      /* Just inside the bound: no failure. */
      reset_model(&S0); busy_polls=24000u/72u-20u; bw_ready_reads=24000u/30u-20u; run(got);
      { uint8_t want[68]; hex(S0_RECORD,want); assert(!br_diag.error && compare(got,want)); }
      puts("measurement and bandwidth poll just within 1 ms: no failure: PASS");
      assert(!br_error_cause(13) && br_error_cause(BR_ERR_VCO_DETECTOR));
      puts("error 13 (r8.1 R2) has no cause line any more: PASS"); }
    return 0;
}
