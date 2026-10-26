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
#define BR_ERR_NO_TRIM 13u         /* R2: no valid stored trim record (no fallback self-calibration) */
/* The log line (at most 43 characters) for a bring-up error whose cause has a name; 0 for the others. */
static const char *br_error_cause(uint32_t code)
{
    return code==BR_ERR_NO_TRIM ? "open BLE: no radio calibration record" :
        code==BR_ERR_VCO_DETECTOR ? "open BLE: VCO detector unusable" : 0;
}
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
    /* R2: a valid stored trim record is required. There is no fallback self-calibration and nothing is written to
     * the calibration sectors: without a record the bring-up fails here, before the radio is programmed. */
    if (!((cal->present&8u) && ble_trim_valid(cal->trim))) br_error(BR_ERR_NO_TRIM);
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
    if (!br_diag.error) { br_diag.calibration=1; br_apply_trim(cal->trim); }   /* the record, checked at stage 1 */
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
