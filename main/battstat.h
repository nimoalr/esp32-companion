/*
 * Battery statistics: how long a charge lasts and how long charging takes,
 * learned from the last few discharge and charge stretches. Kept tiny on
 * purpose: five segments of each kind, a few bytes per segment, one NVS
 * write when a segment ends and one every ten minutes while it runs (so a
 * power-off keeps most of it). Pure C except for the NVS calls.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BATTSTAT_SEGMENTS 5

typedef struct {
    int est_left_min;      /* minutes of battery left at the measured rate, -1 unknown */
    int est_full_min;      /* minutes until full at the measured charge rate, -1 unknown */
    int avg_life_min;      /* a full charge lasts about this long, -1 unknown */
    int avg_charge_min;    /* empty to full takes about this long, -1 unknown */
    int n_discharge, n_charge;    /* segments recorded (max BATTSTAT_SEGMENTS) */
    int run_kind;          /* 0 none, 1 discharging, 2 charging: the stretch being measured now */
    int run_min;           /* ...and how long it has been going */
} battstat_info_t;

void battstat_init(void);
/* Call about once a second with the latest PMIC reading. now_ms: esp_timer based. */
void battstat_update(uint32_t now_ms, bool present, bool vbus, bool charging, int percent);
void battstat_get(int percent, battstat_info_t *out);
