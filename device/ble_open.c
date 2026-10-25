/* SPDX-License-Identifier: MIT OR MPL-2.0
 * Copyright (c) 2026 Julius Vitkauskas */
/* fm1-base peripheral seam. Host work and all lifecycle changes run in main. */
#include "../open_ble/ble_host.h"
#include "../open_ble/ble_store.h"
#include "../open_ble/ble_health.h"
#if FM1B_BLE_CENTRAL
#error "ble = open supports peripheral BLE-MIDI only; disable ble_central"
#endif
static volatile uint8_t radio_quiet,ble_wanted;
static uint8_t radio_last_bad,ble_state,ble_applied,ble_polling;
#define OPEN_LOG_MAGIC 0x4f424c31u
#define OPEN_LOG_N 48u
#define OPEN_LOG_LEN 44u
/* External linkage: warm-reset contents are arbitrary until the magic check. */
struct open_log_s {
    uint32_t magic,total,active,crash_seen;
    char lines[OPEN_LOG_N][OPEN_LOG_LEN];
} open_log __attribute__((section(".noinit")));
static void open_log_init(void)
{
    if(open_log.magic!=OPEN_LOG_MAGIC) {
        memset(&open_log,0,sizeof open_log); open_log.magic=OPEN_LOG_MAGIC;
    }
}
void plat_log(const char *line)
{
    uint32_t k=ble_port_lock(),i=0;
    open_log_init();
    char *p=open_log.lines[open_log.total%OPEN_LOG_N];
    while(line[i] && i<OPEN_LOG_LEN-1u) { p[i]=line[i]; i++; }
    p[i]=0; open_log.total++; ble_port_unlock(k);
}
uint32_t plat_ble_log_total(void) { open_log_init(); return open_log.total; }
const char *plat_ble_log_line(int back)
{
    open_log_init();
    if(back<0 || (uint32_t)back>=open_log.total || (uint32_t)back>=OPEN_LOG_N) return "";
    return open_log.lines[(open_log.total-1u-(uint32_t)back)%OPEN_LOG_N];
}
static void radio_boot_note(void)
{
    open_log_init();
    uint32_t crash=fm1_crash.magic==FM1_CRASH_MAGIC ? fm1_crash.count : 0;
    radio_last_bad=!(fm1_boot.p3_rst&1u) && open_log.active &&
        ((fm1_boot.p3_rst&4u) || crash!=open_log.crash_seen);
    open_log.active=0;
    if(radio_last_bad) plat_log("open BLE previous run crashed: start by hand");
}
static void radio_clean_exit(void) { open_log_init(); open_log.active=0; }
static void ble_start(void) { ble_wanted=1; if(ble_state==BLE_OFF) ble_state=BLE_STARTING; }
void plat_ble_start(void) { ble_start(); }
void plat_ble_enable(int on)
{
    ble_wanted=(uint8_t)!!on;
    if(on && ble_state==BLE_OFF) ble_state=BLE_STARTING;
}
int plat_ble_state(void) { return ble_state; }
int plat_ble_linked(void) { return ble_started && ble_host_linked(); }
int plat_ble_connected(void) { return ble_wanted && ble_started && ble_host_connected(); }
static void ble_poll(void)
{
    if(ble_polling) return;
    if(ble_health.initialized) {
        uint32_t k=ble_port_lock(); ble_health_ms(fm1_ticks()); ble_port_unlock(k);
    }
    ble_polling=1;
    if(ble_wanted && !ble_started && ble_state!=BLE_FAILED) {
        open_log_init(); open_log.total=0; open_log.active=1;
        open_log.crash_seen=fm1_crash.magic==FM1_CRASH_MAGIC ? fm1_crash.count : 0;
        plat_log("open BLE S3.1d / NimBLE 1.9.0 starting");
        ble_health.phase=BLE_HEALTH_STARTING;
        radio_quiet=1;
        if(ble_platform_start()) {
            ble_state=BLE_FAILED; plat_log("open BLE FAILED (see USB log counters)");
        } else ble_applied=1;
        radio_quiet=0;
    }
    if(ble_started) {
        if(ble_applied!=ble_wanted) {
            ble_host_enable(ble_wanted); ble_applied=ble_wanted;
            plat_log(ble_wanted ? "open BLE enabled" : "open BLE disabled; closing links");
            if(ble_wanted) open_log.active=1;
        }
        ble_host_worker();
        if(!ble_wanted) {
            ble_state=BLE_OFF; ble_health.phase=BLE_HEALTH_OFF;
            if(ble_port_storage_idle() && !ble_host_linked()) open_log.active=0;
        } else {
            ble_host_diag_t d; ble_host_get_diag(&d);
            if(ble_port_failed() || !d.running) ble_state=BLE_FAILED;
            else if(d.synced && ble_state!=BLE_READY) { ble_state=BLE_READY; plat_log("open BLE READY"); }
        }
    } else if(!ble_wanted && ble_state!=BLE_FAILED) ble_state=BLE_OFF;
    ble_polling=0;
}
static int ble_midi_in(uint32_t *pkt)
{
    uint32_t ms;
    return ble_started && ble_host_midi_take(pkt,&ms);
}
static void ble_midi_tap(uint32_t pkt) { if(ble_wanted && ble_started) ble_host_midi_out(pkt,fm1_ms); }
static uint32_t ble_midi_out_room(void) { return ble_wanted && ble_started ? ble_host_midi_room() : 0; }
/* Central role intentionally unavailable, even when an app compiles its page. */
void plat_ble_scan(int on) { (void)on; }
int plat_ble_found(int i,char *name,int *rssi) { (void)i; (void)name; (void)rssi; return 0; }
int plat_ble_connect(int i) { (void)i; return -1; }
void plat_ble_disconnect(void) {}
int plat_ble_device(void) { return BLE_DEV_OFF; }

/* A main-context object/raw-data save may pause advertising, never a link.
 * The RAM flash driver rechecks idle atomically at every issued operation. */
static int ble_open_store_begin(void)
{
#if FM1B_BLE_CONNECTED_STORE
    if(ble_store_active) return -8; /* live bond snapshot owns st_buf and SPI0 */
#endif
    if(!ble_started) {
        if(!ble_port_storage_idle()) return -8;
        uint32_t k=ble_port_lock(); ble_health_begin(BLE_GAP_FLASH); ble_port_unlock(k); return 0;
    }
    if(ble_host_pause_advertising()) return -8;
    if(!ble_port_storage_idle()) { ble_host_resume_advertising(); return -8; }
    uint32_t k=ble_port_lock(); ble_health_begin(BLE_GAP_FLASH); ble_port_unlock(k);
    return 0;
}
static void ble_open_store_end(void)
{
    uint32_t k=ble_port_lock(); ble_health_end(BLE_GAP_FLASH); ble_port_unlock(k);
    if(ble_started) ble_host_resume_advertising();
}
