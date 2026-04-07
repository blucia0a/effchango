/*
 * imu_swil.c - Software-in-the-loop IMU implementation
 *
 * Generates synthetic accelerometer data that sweeps back and forth,
 * simulating tilting the device. This produces an audible filter sweep
 * when mapped to LPF cutoff/resonance in the main loop.
 *
 * accel_x: triangle wave, one full cycle per sweep_frames frames
 * accel_y: triangle wave at half the rate (two cycles = one full sweep)
 * accel_z: constant 1000 milli-g (gravity pointing down)
 * gyro: all zero
 */

#include "imu.h"

typedef struct {
    int sweep_frames;
    int frame_count;
} imu_swil_ctx_t;

static imu_swil_ctx_t swil_ctx;

static int swil_init(imu_t *imu) {
    imu_swil_ctx_t *ctx = (imu_swil_ctx_t *)imu->ctx;
    ctx->frame_count = 0;
    return 0;
}

/*
 * Triangle wave: sweeps from -amplitude to +amplitude over period frames.
 * Returns value in [-amplitude, +amplitude].
 */
static int16_t triangle_wave(int frame, int period, int16_t amplitude) {
    int half = period / 2;
    int pos = frame % period;
    if (pos < half) {
        /* Rising: -amplitude to +amplitude */
        return (int16_t)(-amplitude + (2 * amplitude * pos) / half);
    } else {
        /* Falling: +amplitude to -amplitude */
        return (int16_t)(amplitude - (2 * amplitude * (pos - half)) / half);
    }
}

static int swil_read(imu_t *imu, imu_sample_t *sample) {
    imu_swil_ctx_t *ctx = (imu_swil_ctx_t *)imu->ctx;

    sample->accel_x = triangle_wave(ctx->frame_count, ctx->sweep_frames, 1000);
    sample->accel_y = triangle_wave(ctx->frame_count, ctx->sweep_frames * 2, 1000);
    sample->accel_z = 1000; /* gravity */
    sample->gyro_x = 0;
    sample->gyro_y = 0;
    sample->gyro_z = 0;

    ctx->frame_count++;
    return 0;
}

static void swil_shutdown(imu_t *imu) {
    (void)imu;
}

imu_t imu_swil_create(int sweep_frames) {
    swil_ctx.sweep_frames = sweep_frames;
    swil_ctx.frame_count = 0;

    imu_t imu;
    imu.init = swil_init;
    imu.read = swil_read;
    imu.shutdown = swil_shutdown;
    imu.ctx = &swil_ctx;
    return imu;
}
