/*
 * Accelerometer calibration: pure math, no hardware access, host-testable.
 *
 * The wizard captures the mean raw vector in three poses: flat screen-up,
 * upright with the USB port down, and resting on its left edge. In each pose
 * one sensor axis carries gravity and the other two should read zero; their
 * readings estimate the bias, the gravity reading gives the scale. The three
 * "up" readings also define the screen frame (out of the screen, screen up,
 * screen right), so orientation is reported in screen coordinates and
 * does not depend on how the sensor is mounted on the board.
 *
 * Raw units: QMI8658 at +-8 g, 1 g = 4096 LSB.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IMU_CAL_POSES        3
#define IMU_CAL_ONE_G_RAW    4096

typedef struct {
    bool valid;
    int16_t bias[3];        /* raw LSB */
    int32_t scale_q16[3];   /* raw -> g, Q16 (about 1/4096) */
    float rot[3][3];        /* sensor frame -> screen frame (rows: right, down, out of screen) */
} imu_cal_t;

/* Running capture of one pose: keeps a mean and rejects motion. */
typedef struct {
    int32_t sum[3];
    int16_t min[3], max[3];
    int count;
    int target;             /* samples required */
    int16_t max_dev_raw;    /* restart if a sample deviates more than this from the running mean */
    bool done;
    bool restarted;         /* set for one call when motion caused a restart */
} imu_cal_capture_t;

void imu_cal_capture_begin(imu_cal_capture_t *c, int target_samples, int max_dev_mg);
/* Feed a sample. Returns true when the capture just completed. */
bool imu_cal_capture_add(imu_cal_capture_t *c, const int16_t raw[3]);
void imu_cal_capture_mean(const imu_cal_capture_t *c, int32_t mean[3]);

/*
 * Compute bias/scale/up from the three pose means. Returns false with a short
 * reason in `err` when the poses do not look like three distinct axes.
 */
bool imu_cal_compute(const int32_t mean[IMU_CAL_POSES][3], imu_cal_t *out, char *err, int errlen);

/* Defaults for an uncalibrated device: no bias, nominal scale, identity frame. */
void imu_cal_default(imu_cal_t *out);

/* Raw -> calibrated acceleration in g. */
void imu_cal_apply(const imu_cal_t *c, const int16_t raw[3], float g[3]);

/* Calibrated acceleration expressed in the screen frame (x right, y down, z out of the screen). */
void imu_cal_screen(const imu_cal_t *c, const float g[3], float out[3]);

/*
 * Orientation from a screen-frame gravity vector. `ball_x`/`ball_y` in
 * [-1, 1] point toward the low side of the device (0, 0 = level, screen up);
 * pitch (top edge raised = positive) and roll (right edge raised = positive)
 * are in degrees.
 */
void imu_cal_tilt(const float screen_g[3], float *ball_x, float *ball_y, float *pitch_deg, float *roll_deg);
