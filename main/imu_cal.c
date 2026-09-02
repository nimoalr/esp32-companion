#include "imu_cal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void imu_cal_capture_begin(imu_cal_capture_t *c, int target_samples, int max_dev_mg)
{
    memset(c, 0, sizeof(*c));
    c->target = target_samples;
    c->max_dev_raw = (int16_t)((max_dev_mg * IMU_CAL_ONE_G_RAW) / 1000);
    for (int i = 0; i < 3; i++) {
        c->min[i] = INT16_MAX;
        c->max[i] = INT16_MIN;
    }
}

bool imu_cal_capture_add(imu_cal_capture_t *c, const int16_t raw[3])
{
    c->restarted = false;
    if (c->done) return false;
    if (c->count > 0) {
        for (int i = 0; i < 3; i++) {
            const int32_t mean = c->sum[i] / c->count;
            int32_t d = raw[i] - mean;
            if (d < 0) d = -d;
            if (d > c->max_dev_raw) {
                /* motion: start over */
                const int target = c->target;
                const int16_t md = c->max_dev_raw;
                memset(c, 0, sizeof(*c));
                c->target = target;
                c->max_dev_raw = md;
                for (int j = 0; j < 3; j++) { c->min[j] = INT16_MAX; c->max[j] = INT16_MIN; }
                c->restarted = true;
                break;
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        c->sum[i] += raw[i];
        if (raw[i] < c->min[i]) c->min[i] = raw[i];
        if (raw[i] > c->max[i]) c->max[i] = raw[i];
    }
    c->count++;
    if (c->count >= c->target) {
        c->done = true;
        return true;
    }
    return false;
}

void imu_cal_capture_mean(const imu_cal_capture_t *c, int32_t mean[3])
{
    for (int i = 0; i < 3; i++) {
        mean[i] = c->count ? c->sum[i] / c->count : 0;
    }
}

static float dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void normalize(float v[3])
{
    const float n = sqrtf(dot(v, v));
    if (n > 1e-6f) {
        v[0] /= n; v[1] /= n; v[2] /= n;
    }
}

static int dominant_axis(const int32_t v[3])
{
    int a = 0;
    for (int i = 1; i < 3; i++) {
        if (labs(v[i]) > labs(v[a])) a = i;
    }
    return a;
}

bool imu_cal_compute(const int32_t mean[IMU_CAL_POSES][3], imu_cal_t *out, char *err, int errlen)
{
    int dom[IMU_CAL_POSES];
    for (int p = 0; p < IMU_CAL_POSES; p++) {
        dom[p] = dominant_axis(mean[p]);
        const int32_t mag = labs(mean[p][dom[p]]);
        if (mag < IMU_CAL_ONE_G_RAW * 85 / 100 || mag > IMU_CAL_ONE_G_RAW * 115 / 100) {
            snprintf(err, (size_t)errlen, "pose %d: %ld mg on axis %c, expected ~1 g", p + 1, (long)(mag * 1000 / IMU_CAL_ONE_G_RAW), "XYZ"[dom[p]]);
            return false;
        }
        /* the two other axes must be small */
        for (int i = 0; i < 3; i++) {
            if (i != dom[p] && labs(mean[p][i]) > IMU_CAL_ONE_G_RAW * 30 / 100) {
                snprintf(err, (size_t)errlen, "pose %d: not aligned (axis %c = %ld mg)", p + 1, "XYZ"[i], (long)(mean[p][i] * 1000 / IMU_CAL_ONE_G_RAW));
                return false;
            }
        }
    }
    if (dom[0] == dom[1] || dom[0] == dom[2] || dom[1] == dom[2]) {
        snprintf(err, (size_t)errlen, "poses must use three different axes (%c %c %c)", "XYZ"[dom[0]], "XYZ"[dom[1]], "XYZ"[dom[2]]);
        return false;
    }

    /* Bias of each axis: average of its readings in the two poses where it is not vertical. */
    for (int i = 0; i < 3; i++) {
        int32_t acc = 0, n = 0;
        for (int p = 0; p < IMU_CAL_POSES; p++) {
            if (dom[p] != i) { acc += mean[p][i]; n++; }
        }
        out->bias[i] = (int16_t)(n ? acc / n : 0);
    }
    /* Scale of each axis from the pose where it carries gravity. */
    for (int p = 0; p < IMU_CAL_POSES; p++) {
        const int i = dom[p];
        const int32_t one_g = labs(mean[p][i] - out->bias[i]);
        out->scale_q16[i] = one_g ? (int32_t)((1LL << 16) / one_g) : (int32_t)((1LL << 16) / IMU_CAL_ONE_G_RAW);
    }
    /*
     * Screen frame from the calibrated pose vectors. At rest the accelerometer
     * reads +1 g along "up" (opposite to gravity): pose 1 (flat, screen up)
     * reads "out of the screen"; pose 2 (USB port down) reads "screen up",
     * i.e. -down; pose 3 (left edge down) reads "screen right".
     */
    float e_out[3], e_down[3], e_right_meas[3];
    for (int i = 0; i < 3; i++) {
        e_out[i] = (float)(mean[0][i] - out->bias[i]) * (float)out->scale_q16[i] / 65536.f;
        e_down[i] = -(float)(mean[1][i] - out->bias[i]) * (float)out->scale_q16[i] / 65536.f;
        e_right_meas[i] = (float)(mean[2][i] - out->bias[i]) * (float)out->scale_q16[i] / 65536.f;
    }
    normalize(e_out);
    /* Gram-Schmidt: make "down" orthogonal to "out". */
    const float d = dot(e_down, e_out);
    for (int i = 0; i < 3; i++) e_down[i] -= d * e_out[i];
    normalize(e_down);
    /* right = down x out keeps (right, down, out) right-handed. */
    float e_right[3];
    cross(e_down, e_out, e_right);
    normalize(e_right_meas);
    if (dot(e_right, e_right_meas) < 0.5f) {   /* more than 60 degrees off */
        snprintf(err, (size_t)errlen, "pose 3 does not match: was the LEFT edge down?");
        return false;
    }
    for (int i = 0; i < 3; i++) {
        out->rot[0][i] = e_right[i];
        out->rot[1][i] = e_down[i];
        out->rot[2][i] = e_out[i];
    }
    out->valid = true;
    err[0] = 0;
    return true;
}

void imu_cal_default(imu_cal_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 3; i++) {
        out->scale_q16[i] = (int32_t)((1LL << 16) / IMU_CAL_ONE_G_RAW);
        out->rot[i][i] = 1.f;
    }
}

void imu_cal_apply(const imu_cal_t *c, const int16_t raw[3], float g[3])
{
    for (int i = 0; i < 3; i++) {
        g[i] = (float)(raw[i] - c->bias[i]) * (float)c->scale_q16[i] / 65536.f;
    }
}

void imu_cal_screen(const imu_cal_t *c, const float g[3], float out[3])
{
    for (int r = 0; r < 3; r++) {
        out[r] = c->rot[r][0] * g[0] + c->rot[r][1] * g[1] + c->rot[r][2] * g[2];
    }
}

void imu_cal_tilt(const float sg[3], float *ball_x, float *ball_y, float *pitch_deg, float *roll_deg)
{
    float n = sqrtf(dot(sg, sg));
    if (n < 1e-6f) n = 1.f;
    const float x = sg[0] / n, y = sg[1] / n, z = sg[2] / n;
    /* "up" tilts toward the raised side; the ball rolls to the low side. */
    if (ball_x) *ball_x = -x;
    if (ball_y) *ball_y = -y;
    if (pitch_deg) *pitch_deg = atan2f(-y, z) * 57.2957795f;
    if (roll_deg) *roll_deg = atan2f(x, z) * 57.2957795f;
}
