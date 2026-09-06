#include "battstat.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "battstat";

#define NS              "companion"
#define KEY             "batt1"
#define SAVE_EVERY_MS   (10u * 60u * 1000u)
#define MIN_SEG_PCT     3          /* a stretch shorter than this teaches nothing */
#define MIN_SEG_MIN     5

typedef struct {
    uint16_t pct;          /* percent moved */
    uint16_t minutes;      /* over this long */
} seg_t;

typedef struct {
    seg_t dis[BATTSTAT_SEGMENTS], chg[BATTSTAT_SEGMENTS];
    uint8_t ndis, nchg, idis, ichg;
    /* the running stretch, so a power-off in the middle of it still counts */
    uint8_t run_kind, run_start_pct, run_last_pct;
    uint16_t run_minutes;
} store_t;

static store_t s_st;
static uint32_t s_run_start_ms, s_run_last_ms, s_last_save_ms;
static int s_last_pct = -1;

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, KEY, &s_st, sizeof s_st) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void push(seg_t *ring, uint8_t *n, uint8_t *idx, int pct, int minutes)
{
    if (pct < MIN_SEG_PCT || minutes < MIN_SEG_MIN) return;
    ring[*idx] = (seg_t){ (uint16_t)pct, (uint16_t)minutes };
    *idx = (uint8_t)((*idx + 1) % BATTSTAT_SEGMENTS);
    if (*n < BATTSTAT_SEGMENTS) (*n)++;
}

/* Close the running stretch into its ring. */
static void close_run(void)
{
    if (s_st.run_kind == 1) {
        push(s_st.dis, &s_st.ndis, &s_st.idis, (int)s_st.run_start_pct - (int)s_st.run_last_pct, s_st.run_minutes);
    } else if (s_st.run_kind == 2) {
        push(s_st.chg, &s_st.nchg, &s_st.ichg, (int)s_st.run_last_pct - (int)s_st.run_start_pct, s_st.run_minutes);
    }
    s_st.run_kind = 0;
    s_st.run_minutes = 0;
}

void battstat_init(void)
{
    nvs_handle_t h;
    size_t len = sizeof s_st;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, KEY, &s_st, &len) != ESP_OK || len != sizeof s_st) memset(&s_st, 0, sizeof s_st);
        nvs_close(h);
    }
    /* whatever was running when the power went: count what we saved of it */
    if (s_st.run_kind) {
        close_run();
        save();
    }
    ESP_LOGI(TAG, "%u discharge and %u charge stretches on record", s_st.ndis, s_st.nchg);
}

void battstat_update(uint32_t now_ms, bool present, bool vbus, bool charging, int percent)
{
    const int kind = !present ? 0 : (!vbus ? 1 : (charging ? 2 : 0));
    if (kind != s_st.run_kind) {
        if (s_st.run_kind) close_run();
        s_st.run_kind = (uint8_t)kind;
        s_st.run_start_pct = s_st.run_last_pct = (uint8_t)(percent < 0 ? 0 : percent);
        s_st.run_minutes = 0;
        s_run_start_ms = s_run_last_ms = now_ms;
        s_last_save_ms = now_ms;
        save();
        return;
    }
    if (!kind || percent < 0) return;
    s_st.run_last_pct = (uint8_t)percent;
    s_st.run_minutes = (uint16_t)((now_ms - s_run_start_ms) / 60000u);
    if (now_ms - s_last_save_ms >= SAVE_EVERY_MS) {
        s_last_save_ms = now_ms;
        save();
    }
    s_last_pct = percent;
}

/* percent per minute over a ring, weighted by duration; 0 when nothing is known */
static float rate(const seg_t *ring, int n, int *total_min)
{
    int pct = 0, min = 0;
    for (int i = 0; i < n; i++) { pct += ring[i].pct; min += ring[i].minutes; }
    if (total_min) *total_min = min;
    return min > 0 ? (float)pct / (float)min : 0.f;
}

void battstat_get(int percent, battstat_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->est_left_min = out->est_full_min = out->avg_life_min = out->avg_charge_min = -1;
    out->n_discharge = s_st.ndis;
    out->n_charge = s_st.nchg;
    out->run_kind = s_st.run_kind;
    out->run_min = s_st.run_minutes;
    const float rd = rate(s_st.dis, s_st.ndis, NULL);
    const float rc = rate(s_st.chg, s_st.nchg, NULL);
    if (rd > 0.f) {
        out->avg_life_min = (int)(100.f / rd);
        if (percent >= 0) out->est_left_min = (int)((float)percent / rd);
    }
    if (rc > 0.f) {
        out->avg_charge_min = (int)(100.f / rc);
        if (percent >= 0) out->est_full_min = (int)((float)(100 - percent) / rc);
    }
}
