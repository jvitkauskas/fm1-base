/* SPDX-License-Identifier: MIT OR MPL-2.0 */
#pragma once
#include "ble_s3_sim.h"
void sim_radio_start(void);
int sim_radio_receive(uint16_t status,uint16_t header,const uint8_t *data,uint16_t event);
void sim_radio_service(int recovery);
unsigned sim_radio_full(void);
extern void (*sim_radio_release_hook)(void);
extern uint32_t sim_radio_pressure,sim_radio_both,sim_radio_refused;
