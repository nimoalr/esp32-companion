/*
 * Host test for imu_cal.c (no ESP-IDF needed):
 *   tools/host/build.sh imu_cal_test && tools/host/bin/imu_cal_test
 *
 * Simulates a physically right-handed accelerometer mounted in several
 * orientations, captures the three wizard poses and checks that the
 * calibration is accepted and maps gravity back into the screen frame.
 */
#include "imu_cal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/*
 * A mounting is the sensor's basis expressed in a right-handed physical frame
 * P = (right, up, out) of the screen. Rows: sensor x, y, z in P coordinates.
 * The wizard poses in P: flat -> up_dir = out = (0,0,1); USB down -> up_dir =
 * screen up = (0,1,0); left edge down -> up_dir = right = (1,0,0).
 */
typedef struct { const char *name; float m[3][3]; } mount_t;

static void raw_for(const mount_t *mt, const float up_dir[3], const int16_t bias[3], const float scale[3], int16_t raw[3])
{
    for (int i = 0; i < 3; i++) {
        const float g = mt->m[i][0] * up_dir[0] + mt->m[i][1] * up_dir[1] + mt->m[i][2] * up_dir[2];
        raw[i] = (int16_t)lrintf(g * IMU_CAL_ONE_G_RAW * scale[i]) + bias[i];
    }
}

static float det3(const float m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

static void run_mount(const mount_t *mt)
{
    CHECK(fabsf(det3(mt->m) - 1.f) < 1e-4f, "%s: mounting is not right-handed (det %.2f)", mt->name, det3(mt->m));

    static const float poses[IMU_CAL_POSES][3] = { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
    const int16_t bias[3] = { 37, -52, 18 };
    const float scale[3] = { 1.02f, 0.98f, 1.01f };

    int32_t mean[IMU_CAL_POSES][3];
    for (int p = 0; p < IMU_CAL_POSES; p++) {
        imu_cal_capture_t cap;
        imu_cal_capture_begin(&cap, 40, 60);
        bool done = false;
        for (int n = 0; n < 40; n++) {
            int16_t raw[3];
            raw_for(mt, poses[p], bias, scale, raw);
            raw[0] += (int16_t)((n % 3) - 1);     /* a little noise */
            done = imu_cal_capture_add(&cap, raw);
        }
        CHECK(done, "%s: pose %d capture did not complete", mt->name, p + 1);
        imu_cal_capture_mean(&cap, mean[p]);
    }

    imu_cal_t cal;
    char err[64] = "";
    const bool ok = imu_cal_compute(mean, &cal, err, sizeof err);
    CHECK(ok, "%s: compute rejected valid poses: %s", mt->name, err);
    if (!ok) return;

    /* Each pose must come back as its screen-frame unit vector: (x right, y down, z out). */
    static const float expect[IMU_CAL_POSES][3] = { { 0, 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 } };
    for (int p = 0; p < IMU_CAL_POSES; p++) {
        int16_t raw[3];
        raw_for(mt, poses[p], bias, scale, raw);
        float g[3], sg[3];
        imu_cal_apply(&cal, raw, g);
        imu_cal_screen(&cal, g, sg);
        for (int i = 0; i < 3; i++) {
            CHECK(fabsf(sg[i] - expect[p][i]) < 0.03f, "%s: pose %d screen[%d] = %.3f, expected %.1f", mt->name, p + 1, i, sg[i], expect[p][i]);
        }
    }

    /* Tilt: raise the right edge by 20 degrees -> ball rolls left, roll positive. */
    {
        const float a = 20.f * 3.14159265f / 180.f;
        const float up_dir[3] = { sinf(a), 0, cosf(a) };
        int16_t raw[3];
        raw_for(mt, up_dir, bias, scale, raw);
        float g[3], sg[3], bx, by, pitch, roll;
        imu_cal_apply(&cal, raw, g);
        imu_cal_screen(&cal, g, sg);
        imu_cal_tilt(sg, &bx, &by, &pitch, &roll);
        CHECK(bx < -0.3f && fabsf(by) < 0.05f, "%s: ball %.2f %.2f, expected left", mt->name, bx, by);
        CHECK(fabsf(roll - 20.f) < 1.f && fabsf(pitch) < 1.f, "%s: pitch %.1f roll %.1f, expected 0 / 20", mt->name, pitch, roll);
    }
}

int main(void)
{
    /* Sensor axes expressed in (right, up, out); all right-handed. */
    static const mount_t mounts[] = {
        { "identity",         { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } },
        { "rotated 90 cw",    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } } },
        { "rotated 180",      { { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } } },
        { "chip on the back", { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } } },
        { "back, rotated",    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } } },
    };
    for (size_t i = 0; i < sizeof mounts / sizeof mounts[0]; i++) run_mount(&mounts[i]);

    /* Wrong pose 3 (right edge down) must be rejected with a helpful message. */
    {
        int32_t mean[IMU_CAL_POSES][3] = { { 0, 0, 4096 }, { 0, 4096, 0 }, { -4096, 0, 0 } };
        imu_cal_t cal; char err[64] = "";
        CHECK(!imu_cal_compute(mean, &cal, err, sizeof err), "right edge down was accepted");
        CHECK(strstr(err, "LEFT") != NULL, "unexpected error for right edge down: %s", err);
    }
    /* Two poses on the same axis must be rejected. */
    {
        int32_t mean[IMU_CAL_POSES][3] = { { 0, 0, 4096 }, { 0, 0, -4096 }, { 4096, 0, 0 } };
        imu_cal_t cal; char err[64] = "";
        CHECK(!imu_cal_compute(mean, &cal, err, sizeof err), "duplicate axis was accepted");
    }
    /* Motion during capture restarts it. */
    {
        imu_cal_capture_t cap;
        imu_cal_capture_begin(&cap, 10, 60);
        const int16_t still[3] = { 0, 0, 4096 }, moved[3] = { 600, 0, 4096 };
        for (int n = 0; n < 5; n++) imu_cal_capture_add(&cap, still);
        imu_cal_capture_add(&cap, moved);
        CHECK(cap.restarted && cap.count == 1, "motion did not restart the capture (count %d)", cap.count);
    }

    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails ? 1 : 0;
}
