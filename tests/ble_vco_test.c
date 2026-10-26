/* SPDX-License-Identifier: MIT OR MPL-2.0 */
/* Copyright (c) 2026 Julius Vitkauskas */
/* VCO bank table (specification r8.1 R1.3): the pure table and lookup from open_ble/hal/fm1_ble_radio.h, cut
 * verbatim by tests/run_open_ble_tests.py into vco_seam.inc, driven by modelled readings. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vco_seam.inc"

/* The model of R1.3: reading 2 if f < lo(b), 1 if f > lo(b) + w, 0 otherwise. */
static int model,width;
static unsigned reads,bad_reads;
static uint32_t seed;
static int lo(unsigned b)
{
    switch(model) {
    case 1: return 60+2*(100-(int)b);
    case 2: return 40+3*(120-(int)b);
    case 4: return 70+(10-(int)(b&127u));
    default: return 0;
    }
}
static unsigned random_reading(unsigned b,unsigned f)
{
    uint32_t x=seed^(b*0x9e3779b1u)^(f*0x85ebca6bu);
    x^=x>>15; x*=0x2c1b3c6du; x^=x>>12; x*=0x297a2d39u; x^=x>>15;
    return x&3u;
}
static unsigned reading(unsigned b,unsigned f)
{
    reads++;
    if(b>127 || f<BR_VCO_DIV_FIRST || f>BR_VCO_DIV_LAST) bad_reads++;
    if(model==3) return 0;
    if(model==5) return random_reading(b,f);
    if((int)f<lo(b)) return 2;
    if((int)f>lo(b)+width) return 1;
    return 0;
}
/* The definition of R1.2, written out literally (sets of dividers, not one pass) to cross-check the pass. */
static int reference(uint8_t *L)
{
    int k,top=-1;
    unsigned i,f,start=73,u_before=73;
    for(k=0;k<8 && top<0;k++) {
        unsigned a=reading(16u*k,73),b=reading(16u*k+7u,73),c=reading(16u*k+15u,73);
        if(!(a==b && b==c)) top=16*k+15;
    }
    if(top<0) return -1;
    L[47]=(uint8_t)top;
    for(i=0;i<47;i++) {
        unsigned bank=((unsigned)top-i)&255u,u=171,d=start,any_above=0;
        for(f=start;f<=171;f++) if(reading(bank&127u,f)==1) { u=f; any_above=1; break; }
        for(f=start;f<u;f++) if(reading(bank&127u,f)==2) d=f;
        if(!any_above && reading(bank&127u,171)==2) d=171;
        L[i]=(uint8_t)((u_before-3u+d)/2u);
        u_before=u; start=d;
    }
    return 0;
}
static unsigned reference_bank(const uint8_t *L,unsigned f)
{
    unsigned j;
    for(j=1;j<=47;j++) if(L[j]>f) return (L[47]-(j-1u))&255u;
    return (L[47]-47u)&255u;
}
static void check(int m,int w,const uint8_t *expected,unsigned top,unsigned b121,unsigned b119,unsigned b73)
{
    uint8_t L[48],R[48];
    unsigned f,n;
    model=m; width=w; reads=bad_reads=0;
    memset(L,0xee,sizeof L);
    assert(br_vco_table(L,reading)==0);
    n=reads;
    assert(!bad_reads);
    assert(L[47]==top);
    assert(!memcmp(L,expected,47));
    assert(br_vco_bank(L,121)==b121 && br_vco_bank(L,119)==b119 && br_vco_bank(L,73)==b73);
    assert(reference(R)==0 && !memcmp(L,R,48));
    for(f=0;f<256;f++) assert(br_vco_bank(L,f)==reference_bank(L,f));
    printf("M%d: L[47]=%u, banks for 121/119/73 = %u/%u/%u, %u readings: PASS\n",m,L[47],b121,b119,b73,n);
}
int main(void)
{
    static const uint8_t m1[47]={71,72,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111,113,115,
        117,119,121,123,125,127,129,131,133,135,137,139,141,143,145,147,149,151,153,155,157,159,161};
    static const uint8_t m2[47]={71,72,73,76,79,82,85,88,91,94,97,100,103,106,109,112,115,118,121,124,127,130,133,
        136,139,142,145,148,151,154,157,160,163,166,168,169,169,169,169,169,169,169,169,169,169,169,169};
    static const uint8_t m4[47]={71,71,71,71,71,71,72,72,73,73,74,75,76,77,78,79,80,77,77,77,77,77,77,77,77,77,77,
        77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77};
    uint8_t L[48],before[48];
    unsigned s,worst=0;
    check(1,4,m1,95,69,70,93);
    check(2,6,m2,111,93,94,109);
    check(4,3,m4,15,224,224,6);
    /* M3: every reading 0, no group qualifies: a bring-up failure, the table untouched. */
    model=3; reads=0; memset(L,0x5a,sizeof L); memcpy(before,L,sizeof L);
    assert(br_vco_table(L,reading)==-1 && !memcmp(L,before,sizeof L) && reads==24);
    puts("M3: every reading 0: no group, failure, table untouched: PASS");
    /* Readings that do not follow R1.1's assumptions: the one pass still equals the literal definition. */
    model=5;
    for(s=1;s<=2000;s++) {
        uint8_t R[48]; int a,b;
        seed=s*2654435761u; reads=0;
        a=br_vco_table(L,reading); if(reads>worst) worst=reads;
        b=reference(R);
        assert(a==b);
        if(!a) { assert(!memcmp(L,R,48)); assert(br_vco_bank(L,121)==reference_bank(R,121)); }
    }
    printf("2000 arbitrary reading sets equal the literal definition (at most %u readings, bound %u): PASS\n",
           worst,24u+47u*99u);
    return 0;
}
