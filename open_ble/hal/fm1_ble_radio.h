/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
#pragma clang diagnostic push
#pragma clang diagnostic error "-Wall"
#pragma clang diagnostic error "-Wextra"
/* Full 24 MHz recipe, frozen r2 01. Included once by Felucca's HAL unit.
 * Every port poll is bounded. All waits use TIMER4; never alter the system PLL,
 * PMU regulators, WLA_CON0 bit14, or persistent items. */
#pragma once
#include "../ble_s1.h"
#include "../ble_memory.h"
#define BR_R(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define BR_A(n) BR_R(0x11900u+4u*(n))
#define BR_COUNT(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { uint32_t a,b; } br_pair_t;
typedef struct { uint8_t a,b; } br_byte_pair_t;
typedef struct { uint8_t b[4]; } br_vga_t;
#include "fm1_ble_tables.h"
static ble_hw_diag_t br_diag;
static uint8_t br_limits[48],br_dc[16];
static int32_t br_txcmp[5];
static uint32_t br_mark,br_last_feed;
static void br_feed(void)
{
    uint32_t now=fm1_ticks();
    if ((uint32_t)(now-br_last_feed)>=24000000u) {
        fm1_wdt_feed(); br_last_feed=now;
    }
}

static inline void br_sync(void) { __asm__ volatile("csync" ::: "memory"); }
static inline void br_ssync(void) { __asm__ volatile("ssync" ::: "memory"); }
static void br_field(uint32_t addr,uint32_t mask,uint32_t value)
{
    if (!br_diag.error) BR_R(addr)=(BR_R(addr)&~mask)|(value&mask);
}
static void br_af(unsigned n,uint32_t mask,uint32_t value) { br_field(0x11900u+4u*n,mask,value); }
static void br_error(uint32_t code)
{
    if (!br_diag.error) { br_diag.error=code; br_diag.failure_stage=br_diag.stage; }
}
#define BR_ERR_VCO_DETECTOR 12u    /* R1: no bank group gives differing readings at divider 73 */
/* Error 13 (r8.1 R2, no stored trim record) is withdrawn by r8.2 and not reused. 14 and 15: self-calibration. */
static int br_wait(uint32_t addr,uint32_t mask,uint32_t expected)
{
    uint32_t t=fm1_ticks();
    if (br_diag.error) return -1;
    while ((BR_R(addr)&mask)!=expected) {
        if ((uint32_t)(fm1_ticks()-t)>=24000u) { br_error(1); return -1; }
    }
    return 0;
}
static void br_stage(unsigned n)
{
    uint32_t t=fm1_ticks();
    BLE_DIAG_ONLY(if (br_diag.stage) br_diag.step_ticks[br_diag.stage-1]+=t-br_mark);
    (void)t; br_mark=t; br_diag.stage=n;
    fm1_wdt_feed();
}
static void br_phy_write(unsigned r,uint8_t data)
{
    uint32_t v=(r<<8)|data;
    br_feed();
    if (br_wait(0x3101cu,1u<<17,0)) return;
    BR_R(0x3101cu)=v|0x80000u; BR_R(0x3101cu)=v|0xa0000u;
}
static uint8_t br_phy_read(unsigned r)
{
    if (br_diag.error) return 0;
    BR_R(0x3101cu)=(r<<8)|0x90000u;
    BR_R(0x3101cu)=(r<<8)|0xb0000u; br_sync();
    return (uint8_t)BR_R(0x3101cu);
}
static void br_bank_write(unsigned bank,unsigned r,uint8_t value)
{
    br_phy_write(bank+1,(uint8_t)r); br_phy_write(bank+2,value); br_phy_write(bank,1);
}
static uint8_t br_bank_read(unsigned bank,unsigned r)
{
    br_phy_write(bank+1,(uint8_t)r); br_phy_write(bank,2); return br_phy_read(bank+3);
}
static void br_bank_field(unsigned bank,unsigned r,uint8_t mask,uint8_t value)
{
    uint8_t old=br_bank_read(bank,r);
    br_bank_write(bank,r,(uint8_t)((old&~mask)|(value&mask)));
}
static void br_image(unsigned bank,const br_byte_pair_t *image,unsigned n)
{
    unsigned i;
    for (i=0;i<n && !br_diag.error;i++) br_bank_write(bank,image[i].a,image[i].b);
}
static void br_phy_reset(void)
{
    if (br_diag.error) return;
    BR_R(0x31004u)=12; br_phy_write(22,0); br_phy_write(21,0x31);
    br_phy_write(21,0x30); br_phy_write(25,0);
}
static void br_mac_reset(void)
{
    if (br_diag.error) return;
    BR_R(0x31004u)=3; BR_R(0x3030cu)=30; BR_R(0x3030cu)=0; br_phy_reset();
}
static void br_sifs(void)
{
    static const br_pair_t image[]={
        {0x31104,0x00140314},{0x31100,0x33a40c0b},{0x31330,0x00040804},
        {0x31334,0x000c0808},{0x31338,0x000c0104},{0x31348,0x000f1290}};
    unsigned i;
    if (br_diag.error) return;
    for (i=0;i<BR_COUNT(image);i++) BR_R(image[i].a)=image[i].b;
}
static void br_digital_agc(void)
{
    unsigned i;
    for (i=0;i<128 && !br_diag.error;i++) {
        uint32_t w=br_dagc[i];
        int v=(int8_t)w;
        if (v< -64) v= -64;
        if (v>63) v=63;
        br_bank_write(0xc8,0x2f,(uint8_t)i);
        br_bank_write(0xc8,0x2b,(uint8_t)v); br_bank_write(0xc8,0x4e,(uint8_t)(w>>8));
        br_bank_write(0xc8,0x54,(uint8_t)(w>>16)); br_bank_write(0xc8,0x55,(uint8_t)(w>>24));
        br_bank_write(0xc8,0x2f,(uint8_t)(i|128u));
    }
    br_bank_write(0xc8,0x2f,0); br_bank_write(0xc8,0x54,0xc0); br_bank_write(0xc8,0x55,1);
    br_bank_write(0xc8,0x4e,0x42); br_bank_write(0xc8,0x2b,0x42); br_bank_write(0xc8,0x2c,0x42);
    br_bank_write(0xc8,0x79,0); br_bank_write(0xc8,0x7a,0x20);
}
static void br_phy_image(void)
{
    if (br_diag.error) return;
    br_phy_reset(); br_sifs(); BR_R(0x30308u)=0x9407;
    br_digital_agc();
    br_image(0xc8,br_c8,BR_COUNT(br_c8)); br_image(0xd4,br_d4,BR_COUNT(br_d4));
    br_image(0xcc,br_cc,BR_COUNT(br_cc)); br_image(0xd0,br_d0,BR_COUNT(br_d0));
    br_phy_write(106,4); br_phy_write(84,2); br_phy_reset();
}
static void br_linearise(void)
{
    unsigned i;
    br_bank_field(0xd0,81,3,0);
    for (i=0;i<128 && !br_diag.error;i++) {
        uint16_t v=br_rapp[i];
        br_bank_field(0xd0,92,16,16); br_bank_field(0xd0,93,127,(uint8_t)i);
        br_bank_write(0xd0,94,(uint8_t)v); br_bank_field(0xd0,92,32,(uint8_t)((v>>3)&32));
        br_bank_field(0xd0,92,16,0);
    }
    br_bank_write(0xd0,89,0xa0); br_bank_field(0xd0,88,7,0);
    br_bank_write(0xd0,82,100); br_bank_write(0xd0,83,45); br_bank_field(0xd0,81,12,12);
    br_bank_write(0xd0,90,0xa0); br_bank_field(0xd0,88,56,0);
    br_bank_write(0xd0,84,100); br_bank_write(0xd0,85,45); br_bank_field(0xd0,81,48,48);
    br_bank_write(0xd0,91,0x90); br_bank_field(0xd0,88,192,64); br_bank_field(0xd0,92,1,0);
    br_bank_write(0xd0,86,0xd6); br_bank_write(0xd0,87,63); br_bank_field(0xd0,81,192,128);
    br_bank_field(0xd0,81,3,2);
}
static void br_mode(unsigned mode)
{
    /* flags: RX=1 TX=2 TXcal=4 RXcal=8 RXDC=16 */
    static const struct { uint8_t reg,flags; uint32_t mask; } fields[]={
        {0,29,1u<<23},{1,1,1u<<15},{3,25,(1u<<4)|(1u<<5)|(1u<<20)},
        {4,29,(1u<<12)|(1u<<27)},{4,4,1u<<13},{5,29,(1u<<11)|(1u<<18)|(1u<<26)},
        {6,29,3},{2,14,1u<<6},{2,8,1u<<29},{6,14,1u<<19},{7,14,17},
        {8,14,1u<<9},{9,14,(1u<<5)|(1u<<7)|(1u<<8)|(1u<<27)},
        {10,14,(1u<<12)|(1u<<26)},{11,8,1u<<11},{11,4,1u<<14},
        {14,25,1u<<26},{14,14,1u<<27}};
    unsigned i,flag=mode<5 ? 1u<<mode : 0;
    for (i=0;i<BR_COUNT(fields);i++)
        br_af(fields[i].reg,fields[i].mask,(fields[i].flags&flag) ? fields[i].mask : 0);
}
static void br_rn(void)
{
    if (br_diag.error) return;
    br_af(14,(1u<<16)|(1u<<18),0); br_af(13,1u<<24,0); fm1_delay_us(5);
    br_af(14,1u<<16,1u<<16); fm1_delay_us(5);
    br_af(13,1u<<24,1u<<24); br_af(14,1u<<18,1u<<18); fm1_delay_us(100);
}
static uint32_t br_comparator(void)
{
    unsigned i;
    if (br_diag.error) return 0;
    br_af(26,0xf0000000u,0x10000000u);
    for (i=0;i<8;i++) BR_A(30)=1;
    BR_A(30)=0; br_sync(); return BR_A(30);
}
static unsigned br_compare(void)
{
    uint32_t r=br_comparator(); return ((r>>18)&1u)|((r>>16)&2u);
}
/* VCO bank table, specification r8.1 R1. A reading r(bank, divider) is 0..3: 2 = divider below the bank's
 * lock range, 1 = above it, 0/3 = neither. The table and its lookup are pure (no registers), so the host test
 * drives them with modelled readings (tests/ble_vco_test.c); br_vco() supplies the radio's readings. */
#define BR_VCO_DIV_FIRST 73u   /* FB_DIV of the group probes and the first entry's start */
#define BR_VCO_DIV_LAST 171u   /* the highest divider an edge search reads */
#define BR_VCO_DIV_REST 121u   /* FB_DIV and its bank when the table is done */
typedef unsigned (*br_vco_read_t)(unsigned bank7,unsigned divider);
/* limits[47]: the highest bank of the lowest group k (banks 16k..16k+15) whose probes at 16k, 16k+7 and 16k+15
 * are not all equal. Entry i<47 belongs to bank limits[47]-i (mod 256, read through its low 7 bits). One upward
 * pass from the start divider S finds the upper edge U (first "above"; 171 if none) and the lower edge D (last
 * "below" before U; S if none; with no "above", a "below" at 171 gives 171). limits[i]=(U(i-1)-3+D(i))/2 with
 * U(-1)=73, and the next entry starts at D. Returns -1 (table untouched) if no group qualifies. */
static int br_vco_table(uint8_t *limits,br_vco_read_t read)
{
    unsigned k,i,f,top=0,start=BR_VCO_DIV_FIRST,upper_before=BR_VCO_DIV_FIRST;
    int found=0;
    for (k=0;k<8 && !found;k++) {
        unsigned a=read(16u*k,BR_VCO_DIV_FIRST),b=read(16u*k+7u,BR_VCO_DIV_FIRST),c=read(16u*k+15u,BR_VCO_DIV_FIRST);
        if (a!=b || b!=c) { top=16u*k+15u; found=1; }
    }
    if (!found) return -1;
    limits[47]=(uint8_t)top;
    for (i=0;i<47;i++) {
        unsigned bank=(top-i)&127u,upper=BR_VCO_DIV_LAST,lower=start;
        for (f=start;f<=BR_VCO_DIV_LAST;f++) {
            unsigned r=read(bank,f);
            if (r==1) { upper=f; break; }
            if (r==2) lower=f;
        }
        limits[i]=(uint8_t)((upper_before-3u+lower)/2u);
        upper_before=upper; start=lower;
    }
    return 0;
}
/* The bank for divider f: limits[47]-(j-1) for the smallest j in 1..47 with limits[j]>f, else limits[47]-47
 * (mod 256). j runs to 47 on purpose: entry 47 (a bank number) takes part, as specified; limits[0] does not. */
static unsigned br_vco_bank(const uint8_t *limits,unsigned f)
{
    unsigned j;
    for (j=1;j<48 && limits[j]<=f;j++) {}
    return (limits[47]-(j-1u))&255u;
}
static unsigned br_bank_for(unsigned divider) { return br_vco_bank(br_limits,divider); }
static void br_vco_set(unsigned bank,unsigned divider)
{
    br_af(14,127u<<19,(bank&127u)<<19); br_af(15,255u<<5,(divider&255u)<<5);
}
static unsigned br_vco_read(unsigned bank7,unsigned divider)
{
    static unsigned reads;
    if (!(++reads&63u)) fm1_wdt_feed();   /* ~7 ms of readings */
    br_vco_set(bank7,divider); br_rn(); return br_compare();
}
static void br_vco(void)
{
    int rc;
    if (br_diag.error) return;
    br_af(16,1u<<24,1u<<24);                       /* detector on for the readings */
    rc=br_vco_table(br_limits,br_vco_read);
    br_af(16,1u<<24,0); fm1_wdt_feed();
    if (rc) { br_error(BR_ERR_VCO_DETECTOR); return; }
    br_vco_set(br_bank_for(BR_VCO_DIV_REST),BR_VCO_DIV_REST);
    BLE_DIAG_ONLY(memcpy(br_diag.vco,br_limits,sizeof br_limits));
}
static void br_gain(unsigned code)
{
    const uint8_t *b=br_vga[code<42 ? code : 41].b;
    br_af(4,31u<<22,((b[0]&7u)|((b[1]&3u)<<3))<<22);
    br_af(5,(7u<<15)|(7u<<12)|(7u<<5),((b[2]&7u)<<15)|((b[2]&0x70u)<<8)|((b[3]&7u)<<5));
}
static void br_tx(unsigned field,int value)
{
    unsigned packed=field<4 ? 97 : 98,shift=(field<4 ? field : field-4)*2;
    uint16_t v=(uint16_t)value&1023u;
    br_bank_write(0xd0,4+field,(uint8_t)(v>>2));
    br_bank_field(0xd0,packed,(uint8_t)(3u<<shift),(uint8_t)((v&3u)<<shift));
}
static uint16_t br_analog_dc(unsigned q,int v)
{
    uint32_t magnitude=v<0 ? 0u-(uint32_t)v : (uint32_t)v;
    uint16_t code=(uint16_t)((magnitude&511u)|(v>=0 ? 512u : 0u));
    unsigned shift=q ? 17 : 7;
    br_af(2,1023u<<shift,((uint32_t)code&1023u)<<shift); return code;
}
static void br_apply_trim(const uint8_t *t)
{
    unsigned i;
    br_af(7,127u<<8,(uint32_t)t[24]<<8); br_af(8,127,t[25]);
    for (i=0;i<6;i++) br_tx(i,(int32_t)ble_get32(t+4*i));
    br_analog_dc(0,(int32_t)ble_get32(t+32)); br_analog_dc(1,(int32_t)ble_get32(t+36));
    br_bank_write(0xd4,9,t[26]); br_bank_write(0xd4,10,t[28]);
    br_bank_write(0xd4,11,t[27]); br_bank_write(0xd4,12,t[29]);
    if (br_diag.error) return;
    BR_A(23)=ble_get32(t+40); BR_A(24)=ble_get32(t+44);
    br_af(1,15u<<8,8u<<8); br_af(8,255u<<10,(uint32_t)t[42]<<10); br_af(4,255u<<14,(uint32_t)t[42]<<14);
    memcpy(br_dc,t+48,16);
}
/* --- self-calibration (r8.2) begin --- */
/* Radio self-calibration, specification r8.2 C1-C3. It runs in place of the quick trim when there is no valid
 * stored trim record: stages A to G in order, each measuring with the earlier results programmed, then
 * br_selfcal_record() reads the result back from the hardware into a 68-octet record (the quick trim's layout).
 * The record stays in RAM; nothing here writes flash. Hardware access goes only through the helpers above, so the
 * host test (tests/ble_selfcal_test.c) runs this block, cut verbatim, against a model of the chip.
 *
 * Searches (C1.3): every candidate is measured once, in ascending order; the start value wins when its cost equals
 * the minimum, otherwise the first (smallest) candidate with the minimum cost. No shape of the cost is assumed.
 * RX analog DC (stage E): only signs are used. One measurement returns the I and the Q result together (C1.2), so
 * both axes are bisected at once; then each axis is checked at the final pair (its sign there and at a
 * neighbour) and searched again on its own if the check fails, until both hold with the same pair. */
#define BR_ERR_SC_MEASURE 14u      /* C1.2: a power or DC measurement did not finish within 1 ms */
#define BR_ERR_SC_BANDWIDTH 15u    /* C1.2: the bandwidth code was not ready within 1 ms */
#define BR_SC_1MS 24000u           /* TIMER4 ticks */
#define BR_SC_DC_MAX 510           /* stage E range: -510..510 */
#define BR_SC_DC_ROUNDS 8          /* stage E: check-and-search rounds per group (2 suffice on the models) */
enum { BR_SC_LO_I, BR_SC_LO_Q, BR_SC_TX_IDC, BR_SC_TX_QDC, BR_SC_TX_IMB, BR_SC_TX_IPH, BR_SC_TX_QPH,
       BR_SC_RX_IMB, BR_SC_RX_IPH, BR_SC_RX_QPH };
static uint32_t br_sc_measures,br_sc_rx_dc_ticks;   /* measurements in the last run; stage E duration */
/* The measurement sequence (C1.2 steps 1-2), the same for every sel. 0 when the results can be read. */
static int br_sc_measure(void)
{
    uint32_t t;
    if (br_diag.error) return -1;
    if (!(++br_sc_measures&63u)) fm1_wdt_feed();
    br_bank_write(0xd4,21,1); br_bank_write(0xd4,22,1); br_bank_write(0xd4,21,3); br_bank_write(0xd4,22,3);
    t=fm1_ticks();
    for (;;) {
        int late=(uint32_t)(fm1_ticks()-t)>=BR_SC_1MS;   /* taken before the read: a read after 1 ms decides */
        unsigned busy=br_bank_read(0xd4,0x8d)&3u;
        if (br_diag.error) return -1;
        if (!busy) return 0;
        if (late) { br_error(BR_ERR_SC_MEASURE); return -1; }
    }
}
static int32_t br_sc_s16(unsigned hi,unsigned lo)
{
    int32_t v=(int32_t)((hi<<8)|lo);
    return (v&0x8000) ? v-0x10000 : v;
}
/* M(0) and M(3): I x I + Q x Q. All ones after a failure (never a minimum that matters: the run is abandoned). */
static uint32_t br_sc_power(void)
{
    int32_t i,q;
    if (br_sc_measure()) return 0xffffffffu;
    i=br_sc_s16(br_bank_read(0xd4,0x81),br_bank_read(0xd4,0x82));
    q=br_sc_s16(br_bank_read(0xd4,0x84),br_bank_read(0xd4,0x85));
    return (uint32_t)(i*i)+(uint32_t)(q*q);
}
static int br_sc_sign24(unsigned r)
{
    uint32_t v=((uint32_t)br_bank_read(0xd4,r)<<16)|((uint32_t)br_bank_read(0xd4,r+1)<<8)|br_bank_read(0xd4,r+2);
    return (v&0x800000u) ? -1 : v ? 1 : 0;
}
/* Programs the analog DC pair v, takes one measurement and gives the signs of M(1) (axes bit 0) and M(2) (bit 1);
 * an axis not asked for, or a failure, gives 0. */
static void br_sc_dc_signs(const int *v,int *s,unsigned axes)
{
    br_analog_dc(0,v[0]); br_analog_dc(1,v[1]);
    s[0]=s[1]=0;
    if (br_sc_measure()) return;
    if (axes&1u) s[0]=br_sc_sign24(0x81);
    if (axes&2u) s[1]=br_sc_sign24(0x8a);
}
static int br_sc_dc_sign(const int *v,unsigned axis) { int s[2]; br_sc_dc_signs(v,s,1u<<axis); return s[axis]; }
static unsigned br_sc_peak(void)
{
    unsigned peak;
    br_bank_write(0xd4,23,0x82); fm1_delay_us(1000); peak=br_bank_read(0xd4,0x80); br_bank_write(0xd4,23,2);
    return peak;
}
/* Gain setting G: the smallest code 0..36 whose ADC peak exceeds 47, else 36 (whose peak then does not matter). */
static void br_sc_gain_setting(void)
{
    unsigned g;
    fm1_wdt_feed();
    for (g=0;g<36 && !br_diag.error;g++) { br_gain(g); if (br_sc_peak()>47u) break; }
    br_gain(g);
}
static void br_sc_loopback(void)
{
    br_phy_reset();
    if (!br_diag.error) BR_R(0x31004u)=12;
    br_phy_write(25,24);
}
/* TX (con8 = 0b1010, con10 = 3) or RX (0b0110, 1) attenuators. */
static void br_sc_attenuators(uint32_t con8,uint32_t con10)
{
    br_af(8,15u<<20,con8<<20); br_af(9,7u<<28,5u<<28);
    br_af(10,(3u<<13)|(3u<<27),(3u<<13)|(con10<<27)); br_af(4,15u<<8,9u<<8);
}
static void br_sc_set(unsigned what,int v)
{
    switch (what) {
    case BR_SC_LO_I: br_af(7,127u<<8,((uint32_t)v&127u)<<8); break;
    case BR_SC_LO_Q: br_af(8,127u,(uint32_t)v&127u); break;
    case BR_SC_TX_IDC: br_tx(4,v); break;
    case BR_SC_TX_QDC: br_tx(5,v); break;
    case BR_SC_TX_IPH: br_tx(1,v); break;
    case BR_SC_TX_QPH: br_tx(3,v); break;
    case BR_SC_TX_IMB: br_tx(0,v<0 ? 1023+v : 1023); br_tx(2,v<0 ? 1023 : 1023-v); break;
    case BR_SC_RX_IMB:
        br_bank_write(0xd4,9,(uint8_t)(v<0 ? 255+v : 255)); br_bank_write(0xd4,11,(uint8_t)(v<0 ? 255 : 255-v));
        break;
    case BR_SC_RX_IPH: br_bank_write(0xd4,10,(uint8_t)v); break;
    default: br_bank_write(0xd4,12,(uint8_t)v); break;   /* BR_SC_RX_QPH */
    }
}
/* The best value (C1.3) of parameter `what` over first..last from `start`, measured with M(0)/M(3) (the same
 * read); the result is programmed and returned. */
static int br_sc_search(unsigned what,int start,int first,int last)
{
    int v,best=first;
    uint32_t cost,min=0xffffffffu,at_start=0xffffffffu;
    fm1_wdt_feed();
    if (start<first || start>last) { br_sc_set(what,start); at_start=br_sc_power(); }
    for (v=first;v<=last && !br_diag.error;v++) {
        br_sc_set(what,v); cost=br_sc_power();
        if (v==start) at_start=cost;
        if (v==first || cost<min) { min=cost; best=v; }
    }
    if (at_start<=min) best=start;
    br_sc_set(what,best);
    return best;
}
/* Stage E, both axes at once: brackets lo < v < hi with M < 0 at lo and M > 0 at hi; -511 and 511 stand for
 * the ends (the measurement is taken to rise with the value: the fallback rule of C2 E3 points that way). The
 * first measurement is at the start values 0, 0. A closed bracket keeps lo, or the in-range end. */
static void br_sc_dc_bisect(int *v)
{
    int lo[2]={-BR_SC_DC_MAX-1,-BR_SC_DC_MAX-1},hi[2]={BR_SC_DC_MAX+1,BR_SC_DC_MAX+1},s[2];
    unsigned a,open=3;
    v[0]=v[1]=0;
    while (open && !br_diag.error) {
        br_sc_dc_signs(v,s,open);
        for (a=0;a<2;a++) {
            if (!(open&(1u<<a))) continue;
            if (!s[a]) { open&=~(1u<<a); continue; }
            if (s[a]<0) lo[a]=v[a]; else hi[a]=v[a];
            if (hi[a]-lo[a]>1) v[a]=(lo[a]+hi[a])/2;
            else { v[a]=lo[a]<-BR_SC_DC_MAX ? hi[a] : lo[a]; open&=~(1u<<a); }
        }
    }
}
/* Does `axis` satisfy the null condition at the pair v, given its sign s there? M = 0, or the opposite sign at a
 * neighbour (the one the rising slope points to first). At an end of the range with the sign pointing outward
 * there is no null inside the range (rising measurement), and the end is the specified value. */
static int br_sc_dc_holds(const int *v,unsigned axis,int s)
{
    int w[2],n;
    if (!s || (s<0 && v[axis]==BR_SC_DC_MAX) || (s>0 && v[axis]==-BR_SC_DC_MAX)) return 1;
    for (n=0;n<2 && !br_diag.error;n++) {
        w[0]=v[0]; w[1]=v[1]; w[axis]+=((s<0)==(n==0)) ? 1 : -1;
        if (w[axis]<-BR_SC_DC_MAX || w[axis]>BR_SC_DC_MAX) continue;
        if (br_sc_dc_sign(w,axis)==-s) return 1;
    }
    return 0;
}
/* One axis on its own, the other held: from v (sign s, not 0) step outward 1, 2, 4, ... until the sign changes,
 * then bisect that gap. The rising direction is tried first, then the other. No change anywhere: -510 if
 * positive, +510 if negative (C2 E3). */
static void br_sc_dc_refine(int *v,unsigned axis,int s)
{
    int pass,w[2];
    w[0]=v[0]; w[1]=v[1];
    for (pass=0;pass<2 && !br_diag.error;pass++) {
        int dir=((s<0)==(pass==0)) ? 1 : -1,from=v[axis],step=1;
        for (;;) {
            int x=from+dir*step,sx;
            if (x>BR_SC_DC_MAX) x=BR_SC_DC_MAX;
            if (x< -BR_SC_DC_MAX) x= -BR_SC_DC_MAX;
            if (x==from) break;
            w[axis]=x; sx=br_sc_dc_sign(w,axis);
            if (br_diag.error) return;
            if (!sx) { v[axis]=x; return; }
            if (sx==-s) {
                while (x-from>1 || from-x>1) {          /* M has sign s at from and -s at x */
                    int m=(from+x)/2;
                    w[axis]=m; sx=br_sc_dc_sign(w,axis);
                    if (!sx || br_diag.error) { v[axis]=m; return; }
                    if (sx==s) from=m; else x=m;
                }
                v[axis]=from; return;
            }
            from=x; step*=2;
        }
    }
    v[axis]=s>0 ? -BR_SC_DC_MAX : BR_SC_DC_MAX;
}
/* The null of one group (C2 E3), left in v; the last pair measured is not necessarily v. */
static void br_sc_dc_null(int *v)
{
    int round,s[2];
    br_sc_dc_bisect(v);
    for (round=0;round<BR_SC_DC_ROUNDS && !br_diag.error;round++) {
        int changed=0;
        br_sc_dc_signs(v,s,3);
        if (!br_sc_dc_holds(v,0,s[0])) { br_sc_dc_refine(v,0,s[0]); changed=1; s[1]=br_sc_dc_sign(v,1); }
        if (!br_sc_dc_holds(v,1,s[1])) { br_sc_dc_refine(v,1,s[1]); changed=1; }
        if (!changed) break;                            /* both hold with this pair */
    }
}
/* Stage E: the four RX gain groups' null pairs into the tune table br_dc; the last pair stays programmed. */
static void br_sc_rx_dc(void)
{
    static const uint8_t groups[4][3]={{0,0,127},{1,0,127},{1,3,69},{1,9,14}};
    uint32_t start=fm1_ticks();
    unsigned g;
    br_phy_reset(); br_sc_loopback();
    br_bank_write(0xd0,0,3); br_bank_write(0xd0,95,16);
    br_bank_write(0xd4,0,62); br_bank_write(0xd4,27,56); br_bank_write(0xd4,8,0xc0); br_bank_write(0xd4,19,0);
    br_bank_write(0xd4,20,56); br_bank_write(0xd4,20,52);
    br_gain(40);
    for (g=0;g<4 && !br_diag.error;g++) {
        int v[2];
        fm1_wdt_feed();
        br_af(3,3u<<6,(uint32_t)groups[g][0]<<6);
        br_af(4,(15u<<8)|127u,((uint32_t)groups[g][1]<<8)|groups[g][2]);
        br_analog_dc(0,0); br_analog_dc(1,0);
        br_sc_dc_null(v);
        ble_put16(br_dc+4*g,br_analog_dc(0,v[0])); ble_put16(br_dc+4*g+2,br_analog_dc(1,v[1]));
    }
    br_sc_rx_dc_ticks=fm1_ticks()-start;
}
/* Bandwidth code for one WLA_CON26 configuration (bits [9:0]; [17:10] = 0x1E, [23:18] = 0, start bit clear). */
static uint32_t br_sc_bandwidth(uint32_t config)
{
    uint32_t t,code;
    br_af(26,0xffffffu,config|(0x1eu<<10));
    br_af(26,1u,1u);
    t=fm1_ticks();
    for (;;) {
        int late=(uint32_t)(fm1_ticks()-t)>=BR_SC_1MS;
        uint32_t r=br_comparator();
        if (br_diag.error) return 0;
        if (r&32u) break;
        if (late) { br_error(BR_ERR_SC_BANDWIDTH); return 0; }
    }
    code=(BR_A(30)>>8)&255u;
    br_af(26,1u,0);
    return code;
}
static void br_selfcal_run(void)
{
    static const uint16_t config[3]={0x1fc,0x122,0x0a0};
    uint32_t code[3],word;
    unsigned i;
    int k;
    br_sc_measures=0; br_sc_rx_dc_ticks=0;
    fm1_wdt_feed();
    /* A: bandwidth */
    br_af(1,15u<<8,8u<<8); br_af(20,1u<<27,1u<<27);
    for (i=0;i<3;i++) code[i]=br_sc_bandwidth(config[i]);
    br_af(20,1u<<27,0);
    if (br_diag.error) return;
    word=0xffu|(code[0]<<8)|(code[1]<<16)|(code[2]<<24);
    BR_A(23)=word; BR_A(24)=word;
    br_af(8,255u<<10,code[1]<<10); br_af(4,255u<<14,code[1]<<14);
    /* B */
    br_mode(2); br_sc_attenuators(10,3);
    /* C: TX I/Q, M(0) */
    br_phy_reset();
    br_bank_write(0xc8,0x15,0); br_bank_write(0xc8,0x14,0);
    br_bank_field(0xc8,0x40,0x80,0); br_bank_field(0xc8,0x41,0x80,0);
    br_bank_write(0xc8,0x28,0); br_bank_write(0xc8,0x27,5);
    br_phy_write(4,(uint8_t)(br_phy_read(4)&0xe7u));
    br_sc_loopback();
    br_bank_write(0xd0,0,3); br_bank_write(0xd0,95,16);
    br_bank_write(0xd0,1,20); br_bank_write(0xd0,2,0); br_bank_write(0xd0,3,0);
    br_bank_write(0xd4,0,62); br_bank_write(0xd4,19,30); br_bank_write(0xd4,20,56); br_bank_write(0xd4,20,52);
    br_tx(0,1023); br_tx(1,0); br_tx(2,1023); br_tx(3,0); br_tx(4,0); br_tx(5,0);
    br_sc_set(BR_SC_LO_I,64); br_sc_set(BR_SC_LO_Q,64);
    br_sc_gain_setting();
    br_sc_search(BR_SC_LO_I,64,0,127);
    br_sc_search(BR_SC_LO_Q,64,0,127);
    br_tx(4,0); br_tx(5,0); br_sc_gain_setting();
    br_sc_search(BR_SC_TX_IDC,0,-128,127);
    br_sc_search(BR_SC_TX_QDC,0,-128,127);
    br_bank_write(0xd4,19,60); br_tx(0,1023); br_tx(2,1023);
    k=br_sc_search(BR_SC_TX_IMB,0,-127,127);
    br_tx(1,0); br_tx(3,0);
    br_sc_search(k>=0 ? BR_SC_TX_QPH : BR_SC_TX_IPH,0,-200,200);
    if (br_diag.error) return;
    /* D */
    br_af(5,31u,0); br_mode(0); br_af(1,1u<<15,0);
    /* E: RX analog DC, M(1)/M(2) */
    br_sc_rx_dc();
    if (br_diag.error) return;
    /* F */
    br_af(5,31u,25u); br_mode(3); br_sc_attenuators(6,1);
    /* G: RX I/Q, M(3) */
    br_phy_reset(); br_sc_loopback();
    br_bank_write(0xd0,0,3); br_bank_write(0xd0,95,16);
    br_bank_write(0xd0,1,20); br_bank_write(0xd0,2,0); br_bank_write(0xd0,3,17);
    br_bank_write(0xd4,9,0xff); br_bank_write(0xd4,11,0xff); br_bank_write(0xd4,10,0); br_bank_write(0xd4,12,0);
    br_bank_write(0xd4,0,62); br_bank_write(0xd4,20,56); br_bank_write(0xd4,20,4); br_bank_write(0xd4,19,30);
    br_sc_gain_setting();
    k=br_sc_search(BR_SC_RX_IMB,0,-105,105);
    br_sc_search(k>=0 ? BR_SC_RX_QPH : BR_SC_RX_IPH,0,-50,50);
    fm1_wdt_feed();
}
/* The 68-octet record (C3) from the values now in the hardware; the check word is CRC-16/XMODEM of 0..63. */
static void br_selfcal_record(uint8_t out[68])
{
    unsigned f,i,packed[2];
    uint32_t dc;
    packed[0]=br_bank_read(0xd0,97); packed[1]=br_bank_read(0xd0,98);
    for (f=0;f<6;f++) {
        uint32_t v=((uint32_t)br_bank_read(0xd0,4+f)<<2)|((packed[f/4]>>(2u*(f%4)))&3u);
        if (f!=0 && f!=2 && (v&512u)) v|=0xfffffc00u;   /* phases and DC: signed 10-bit, sign-extended */
        ble_put32(out+4*f,v);
    }
    out[24]=(uint8_t)((BR_A(7)>>8)&127u); out[25]=(uint8_t)(BR_A(8)&127u);
    out[26]=br_bank_read(0xd4,9); out[27]=br_bank_read(0xd4,11);
    out[28]=br_bank_read(0xd4,10); out[29]=br_bank_read(0xd4,12);
    out[30]=0; out[31]=0;
    dc=BR_A(2);
    for (i=0;i<2;i++) {
        uint32_t code=(dc>>(i ? 17 : 7))&1023u,magnitude=code&511u;
        ble_put32(out+32+4*i,(code&512u) ? magnitude : 0u-magnitude);   /* sign bit set: value >= 0 */
    }
    ble_put32(out+40,BR_A(23)); ble_put32(out+44,BR_A(24));
    for (i=0;i<16;i++) out[48+i]=br_dc[i];
    ble_put32(out+64,ble_crc16(0,out,64));
}
/* --- self-calibration (r8.2) end --- */
/* The log line (at most 43 characters) for a bring-up error whose cause has a name; 0 for the others. */
static const char *br_error_cause(uint32_t code)
{
    return code==BR_ERR_SC_MEASURE ? "open BLE: self-cal measurement timeout" :
        code==BR_ERR_SC_BANDWIDTH ? "open BLE: self-cal bandwidth timeout" :
        code==BR_ERR_VCO_DETECTOR ? "open BLE: VCO detector unusable" : 0;
}
static int br_signed10(unsigned v) { return (v&512u) ? (int)v-1024 : (int)v; }
static void br_handover(void)
{
    unsigned r4=br_bank_read(0xd0,4),r6=br_bank_read(0xd0,6);
    unsigned r5=br_bank_read(0xd0,5),r7=br_bank_read(0xd0,7);
    unsigned r8=br_bank_read(0xd0,8),r9=br_bank_read(0xd0,9);
    unsigned p=br_bank_read(0xd0,97),d=br_bank_read(0xd0,98);
    br_txcmp[0]=(int32_t)(r4>>1); br_txcmp[1]=(int32_t)(r6>>1);
    br_txcmp[2]=(br_signed10((r5<<2)|((p>>2)&3u))+br_signed10((r7<<2)|((p>>6)&3u)))/8;
    br_txcmp[3]=br_signed10((r8<<2)|(d&3u))/4;
    br_txcmp[4]=br_signed10((r9<<2)|((d>>2)&3u))/4;
}
static int br_seq_wait(void)
{
    uint32_t t=fm1_ticks();
    if (br_diag.error) return -1;
    while (BR_R(0x14028u)&0x700000u) {
        BR_R(0x14034u)=0;
        if ((uint32_t)(fm1_ticks()-t)>=24000u) { br_error(5); return -1; }
    }
    return 0;
}
static void br_strobe(void)
{
    unsigned i;
    for (i=0;i<5;i++) BR_R(0x14028u)|=16;
    for (i=0;i<5;i++) BR_R(0x14028u)&=~16u;
}
static void br_sequencer(const uint8_t *pa)
{
    unsigned i;
    if (br_diag.error) return;
    BR_R(0x14028u)=0; BR_R(0x1402cu)=0; BR_R(0x14030u)=0; BR_R(0x14028u)=0x10000;
    if (br_seq_wait()) return;
    BR_R(0x1402cu)=0; BR_R(0x14030u)=0; BR_R(0x14028u)=0x10000; br_strobe();
    for (i=0;i<256 && !br_diag.error;i++) {
        uint32_t a=br_seq[i].a,b=br_seq[i].b;
        if (i<128) b|=(uint32_t)br_bank_for((b>>21)&255u)<<9;
        else if (i<224) {
            unsigned g=i<133 ? 0 : i<180 ? 1 : i<186 ? 2 : 3;
            a=(a&4095u)|((uint32_t)(ble_get16(br_dc+4*g)&1023u)<<12)|
                ((uint32_t)(ble_get16(br_dc+4*g+2)&1023u)<<22);
        } else {
            a=(a&~63u)|(pa[5]&7u)|((pa[6]&7u)<<3);
            b=(b&~((7u<<9)|(7u<<15)|(7u<<18)|(7u<<23)|(15u<<26)))|
                ((pa[0]&7u)<<9)|((pa[1]&7u)<<15)|((pa[2]&7u)<<18)|
                ((pa[3]&7u)<<23)|((pa[4]&15u)<<26);
        }
        if (br_seq_wait()) return;
        BR_R(0x1402cu)=a; BR_R(0x14028u)=(i<<8)|0x1000e; br_strobe();
        if (br_seq_wait()) return;
        BR_R(0x1402cu)=b; BR_R(0x14028u)=(i<<8)|0x1000d; br_strobe();
    }
    if (br_seq_wait()) return;
    for (i=0;i<6;i++) BR_R(0x14028u)=0;
    BR_R(0x14028u)=0x10000;
}
static inline void br_p33_snapshot(uint8_t *out)
{
    unsigned i,n=0;
    for (i=0;i<=0x17;i++) out[n++]=fm1_p33_read(i);
    for (i=0x30;i<=0x35;i++) out[n++]=fm1_p33_read(i);
    for (i=0x70;i<=0x77;i++) out[n++]=fm1_p33_read(i);
    br_diag.p33_timeouts=fm1_p33_timeouts;
}
static void br_clock_probe(void)
{
    uint32_t n=100000,t=fm1_ticks(),dt;
    /* Unknown cycle cost/cache state: this measures this loop's throughput,
     * not CPU Hz. Every radio wait uses TIMER4 independently (r2b 04 §0.4). */
    do { __asm__ volatile("nop"); } while (--n);
    dt=fm1_ticks()-t; br_diag.loop_ticks=dt;
    if (!dt) br_error(10);
    else br_diag.loop_iters_per_us_x1000=2400000000u/dt;
    BLE_DIAG_ONLY(br_diag.clocks[0]=BR_R(0x1000cu); br_diag.clocks[1]=BR_R(0x10010u););
    BLE_DIAG_ONLY(br_diag.clocks[2]=BR_R(0x10014u); br_diag.clocks[3]=BR_R(0x10008u); /* SYS_DIV, public WL82.h */);
}
static void br_radio_init(ble_cal_t *cal,uint32_t *modem)
{
    static const struct { uint8_t n; uint32_t mask,value; } analog[]={
        {0,0xfffc3fff,0xea200005},{1,0xffffffff,0x01c35880},{2,0xffffffff,0x9c010010},
        {3,0xffffffff,0x672b9349},{4,0x3fffffff,0x0508098e},{5,0x7fffffff,0x4078c319},
        {6,0x000fffff,0x00076a24},{7,0xffffffff,0x2915c066},{8,0x3fffffff,0x00a641c0},
        {9,0x7fffffff,0x65b80057},{10,0x1fffffff,0x1afae7e7},{11,0x007fffff,0x0052a070},
        {13,0x3fffffff,0x01000000},{14,0xffffffff,0x819bd033},{15,0x7fffffff,0x3a780ef2},
        {16,0x3fffffff,0x08b41490},{17,0x000fffff,0x00000a80},{25,0x00030000,0x00010000}};
    static const uint8_t mcs_regs[12]={11,13,15,17,27,29,31,33,19,21,23,25};
    static const uint8_t mcs_idx[12]={0,1,2,3,5,7,9,11,12,14,16,19};
    static const uint8_t mcs_default[12]={0x20,0x20,0x20,0x20,0x20,0x2b,0x26,0x20,0x20,0x2b,0x26,0x20};
    unsigned i;
    uint32_t start=fm1_ticks();
    BLE_DIAG_ONLY(br_diag.clocks[0]=BR_R(0x1000cu); br_diag.clocks[1]=BR_R(0x10010u););
    BLE_DIAG_ONLY(br_diag.clocks[2]=BR_R(0x10014u); br_diag.clocks[3]=BR_R(0x10008u););
    BLE_DIAG_ONLY(br_p33_snapshot(br_diag.p33_before););
    br_stage(1);
    br_field(0x10010u,3u<<14,0);
    for (i=0;i<5 && !br_diag.error;i++) BR_R(0x14040u+4*i)=0;
    br_field(0x14040u,1u<<8,1u<<8); br_field(0x14040u,1u<<6,1u<<6);
    br_field(0x14040u,1,1); fm1_delay_us(1); br_field(0x14040u,4,4); fm1_delay_us(1);
    br_field(0x14040u,8,8); fm1_delay_us(1); br_field(0x14040u,2,2); fm1_delay_us(50);
    br_field(0x1405cu,255,5); br_field(0x1405cu,0xffff00,0);
    br_field(0x1405cu,0xc7000000u,0x47000000u);
    br_stage(2);
    br_field(0x1404cu,1u<<24,1u<<24);
    { static const uint8_t bits[]={11,10,9,5,4,3,2,1,0};
      for (i=0;i<BR_COUNT(bits);i++) br_field(0x30f04u,1u<<bits[i],bits[i]==3 ? 0 : 1u<<bits[i]); }
    br_field(0x30f00u,1u<<10,1u<<10); br_field(0x30f00u,3u<<8,0);
    br_field(0x30f00u,7u<<5,3u<<5); br_field(0x30f00u,16,0);
    for (i=4;i>0;i--) br_field(0x30f00u,1u<<(i-1),1u<<(i-1));
    br_stage(3);
    br_mac_reset(); br_sifs(); if (!br_diag.error) BR_R(0x30308u)=0x9407;
    br_mac_reset(); br_image(0xd0,br_cfr_a,BR_COUNT(br_cfr_a)); br_phy_image();
    br_stage(4);
    br_af(12,0x0fffffffu,0x0580025fu|((cal->xtal[0]&15u)<<13)|((cal->xtal[1]&15u)<<19));
    if (!br_diag.error) BR_A(20)=0;
    for (i=0;i<BR_COUNT(analog);i++) br_af(analog[i].n,analog[i].mask,analog[i].value);
    br_stage(5); br_mode(5); br_rn(); br_vco();
    br_af(13,0x0effffffu,0x00c00000u); br_af(14,4095,101);
    br_af(16,1u| (3u<<6),1); br_af(15,(15u<<18)|(7u<<27),(9u<<18)|(2u<<27));
    if (!br_diag.error) BR_A(21)=0x02822520;
    br_rn(); br_af(14,1u<<18,0); fm1_delay_us(80); br_af(14,1u<<18,1u<<18);
    br_stage(6);
    if (!br_diag.error) {
        if ((cal->present&8u) && ble_trim_valid(cal->trim)) { br_diag.calibration=1; br_apply_trim(cal->trim); }
        else {
            /* r8.2 C2/C3: no valid stored record: self-calibrate in place of the quick trim. The record is kept
             * in RAM (cal->trim) for the diagnostics and never written to flash; present bit 3 stays clear, so
             * every boot self-calibrates again. */
            br_diag.calibration=2; br_selfcal_run();
            if (!br_diag.error) br_selfcal_record(cal->trim);
            BLE_DIAG_ONLY(br_diag.selfcal_rx_dc_ticks=br_sc_rx_dc_ticks; br_diag.selfcal_measures=br_sc_measures);
        }
    }
    br_stage(7); br_mode(0);
    br_af(9,7u<<24,(cal->pa[0]&7u)<<24); br_af(10,7,cal->pa[1]);
    br_af(10,7u<<9,(cal->pa[2]&7u)<<9); br_af(10,7u<<15,(cal->pa[3]&7u)<<15);
    br_af(10,15u<<22,(cal->pa[4]&15u)<<22); br_af(11,7,cal->pa[5]);
    br_stage(8); br_handover();
    br_stage(9);
    br_af(1,(3u<<16)|(7u<<12),(3u<<16)|(4u<<12)); br_af(2,7,0);
    br_af(3,3u<<6,1u<<6); br_af(4,(15u<<8)|127u,(9u<<8)|14u); br_gain(0);
    br_stage(10); br_phy_image();
    br_stage(11); br_image(0xd0,br_cfr_b,BR_COUNT(br_cfr_b)); br_linearise();
    br_stage(12); br_sequencer(cal->pa);
    br_stage(13);
    if (!br_diag.error) BR_A(20)=0x57bfffdc;
    br_af(9,15u<<5,15u<<5); br_af(14,3u<<26,3u<<26); br_af(25,0xff000000u,0x3b000000u);
    br_field(0x1405cu,0xc7000000u,0x47000000u); br_field(0x1405cu,0xff0000,0x60000);
    br_field(0x1405cu,255,0x6b);
    for (i=0;i<12;i++) br_bank_write(0xd0,mcs_regs[i],cal->mcs[mcs_idx[i]] ? cal->mcs[mcs_idx[i]] : mcs_default[i]);
    br_stage(14);
    /* Vendor loop240 = 5.3 us; conservative fixed 10 us, no CPU assumption. */
    br_field(0x10010u,3u<<14,0); fm1_delay_us(10);
    br_field(0x14000u,(1u<<7)|(31u<<16),(1u<<7)|(12u<<16)); fm1_delay_us(10);
    br_field(0x14000u,1,1); fm1_delay_us(10);
    br_field(0x2fc40u,1u<<10,1u<<10); br_field(0x20000u,3u<<30,3u<<30); fm1_delay_us(10);
    if (!br_diag.error) { BR_R(0x2fc40u)=65535; BR_R(0x2fc40u)&=~0x302u; BR_R(0x2fc78u)=15; BR_R(0x2fc78u)|=0x3f0; }
    br_stage(15);
    if (!br_diag.error) {
        BR_R(0x2fc88u)=(uint32_t)(uintptr_t)modem;
        for (i=0;i<81;i++) { modem[3*i]=(i<<8)|i; modem[3*i+1]=modem[3*i+2]=0; }
        br_sync(); BR_R(0x2fd98u)=0;
        for (i=0;i<64;i++) { BR_R(0x2fd9cu)=br_agc[i].a; BR_R(0x2fd9cu)=br_agc[i].b; }
        BR_R(0x2fc20u)&=~(3u<<30);
        BR_R(0x2fd80u)=0x000f0000; BR_R(0x2fd84u)=0x1872bf14; BR_R(0x2fd88u)=0x1872bf55;
        BR_R(0x2fd8cu)=0x1872bf00; BR_R(0x2fd90u)=0; BR_R(0x2fd94u)=0; fm1_delay_us(10);
        BR_R(0x2fd80u)|=1; BR_R(0x2fd80u)&=~(1u<<20); BR_R(0x2fc7cu)|=1;
        BR_R(0x2fc48u)=0xb9; BR_R(0x2fc00u)=0x00ffd144; BR_R(0x2fc04u)=0x4e143cdf;
        BR_R(0x2fc08u)=0x03100000; BR_R(0x2fc0cu)=0x80808080; BR_R(0x2fc10u)=0x80008080;
        BR_R(0x2fc14u)=128; BR_R(0x2fc18u)=0; BR_R(0x2fc1cu)=0;
        BR_R(0x2fc20u)=0x36555537; BR_R(0x2fc24u)=0x00034041; BR_R(0x2fc28u)=0x0005c000;
        br_field(0x2fc10u,0xffffffu,(uint32_t)br_txcmp[0]|((uint32_t)br_txcmp[1]<<8)|((uint32_t)(-br_txcmp[2])&255u)<<16);
        br_field(0x2fc08u,0xfffffu,((uint32_t)br_txcmp[3]&1023u)|(((uint32_t)br_txcmp[4]&1023u)<<10));
        br_field(0x2fc9cu,15,8); BR_R(0x2fc98u)=0x02360236;
        br_field(0x2fc9cu,0xffff0000u,0x36360000); fm1_delay_us(10); br_field(0x2fca0u,65535,0x3636);
    }
    br_stage(16); br_diag.bringup_ticks=fm1_ticks()-start;
    if (br_diag.error) {
        /* Before BB opens, request only the specified manual "all off" mode.
         * No undocumented abort/reset/clock gate; a bus hang may still defeat it. */
        uint32_t error=br_diag.error;
        br_diag.error=0; BR_A(20)=0; br_mode(5); br_sync(); br_diag.error=error;
    }
    BLE_DIAG_ONLY(br_p33_snapshot(br_diag.p33_after););
}

#pragma clang diagnostic pop
