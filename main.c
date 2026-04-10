/*
 * main.c - Efficient E1x Chango main loop
 *
 * Runs the chango pipeline in a frame loop:
 *   camera.capture() → scale_image() → pixelate() → synthesize() →
 *   lowpass() → audio_out.write()
 *
 * Camera captures at source resolution (e.g., 320x240 QVGA), then
 * scale_image() resizes to 256x256 for power-of-2 grid processing.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AUTOBENCH
#include <stopprop.h>
#else
#include "stopprop.h"
#endif

#define DEFINE_CHANGO_DATA
#ifndef AUTOBENCH
#include <chango_data.h.inc>
#else
#include "chango_data.h"
#endif

#include "camera.h"
#include "audio_out.h"
#include "imu.h"

/* Kernel declarations - NO division inside any of these */
void scale_image(const uint8_t *restrict src,
                 int src_w, int src_h,
                 uint8_t *restrict dst);

void pixelate(const uint8_t *restrict image,
              int width,
              int region_w, int region_h,
              int grid_x, int grid_y,
              int region_shift,
              uint8_t *restrict intensities);

void synthesize(const uint8_t *restrict intensities,
                int num_tones,
                int32_t inv_num_tones,
                int inv_shift,
                const int16_t *restrict sine_table,
                const uint32_t *restrict phase_incs,
                uint32_t *restrict phases,
                int16_t *restrict audio_out,
                int num_samples);

void lowpass(const int16_t *restrict input,
             int16_t *restrict output,
             int length,
             int32_t b0, int32_t b1, int32_t b2,
             int32_t a1, int32_t a2,
             int32_t *restrict state);

#define SAMPLES_PER_FRAME 512

/* Total frames to render (SWIL mode). */
#define NUM_FRAMES (NUM_AUDIO_SAMPLES / SAMPLES_PER_FRAME)

/* IMU sweep period in frames. One full tilt cycle over the entire render. */
#define IMU_SWEEP_FRAMES (NUM_AUDIO_SAMPLES / SAMPLES_PER_FRAME)

/*
 * Precomputed constants to avoid division on the fabric.
 *
 * For pixelate: 256x256 image / 4x4 grid = 64x64 regions = 4096 pixels
 * For synthesize: 16 tones is a power of 2 → pure shift
 */
#define REGION_W (IMG_WIDTH / NUM_GRID_X)   /* 64 */
#define REGION_H (IMG_HEIGHT / NUM_GRID_Y)  /* 64 */
#define REGION_SHIFT 12  /* log2(64 * 64) = log2(4096) = 12 */

#define INV_NUM_TONES 1  /* 16 is power of 2, so just shift */
#define INV_SHIFT 4      /* log2(16) = 4 */

/* Buffers */
uint8_t scaled_image[256 * 256];  /* output of scale_image */
uint8_t intensities[NUM_TONES];
uint32_t phases[NUM_TONES];
int16_t audio_buf[SAMPLES_PER_FRAME];
int16_t filtered_buf[SAMPLES_PER_FRAME];
int32_t lpf_state[4]; /* {x1, x2, y1, y2} */

#define OUTPUT_FILE "chango_output.raw"

int main() {
    /* Prevent constant propagation of inputs */
    stop_propagation_u8((uint8_t *)test_image, SRC_WIDTH * SRC_HEIGHT);
    stop_propagation_i16((int16_t *)sine_table, SINE_TABLE_SIZE);

    /* Set up camera (SWIL: returns test image at source resolution) */
    camera_t cam = camera_swil_create(test_image, SRC_WIDTH, SRC_HEIGHT);
    if (cam.init(&cam) != 0) {
        printf("[chango] FAIL - camera init\n");
        return 1;
    }

    /* Set up audio output (SWIL: raw binary bytes over UART) */
    audio_out_t ao = audio_out_swil_create(OUTPUT_FILE, SAMPLE_RATE);
    if (ao.init(&ao) != 0) {
        printf("[chango] FAIL - audio output init\n");
        cam.shutdown(&cam);
        return 1;
    }

    /* Set up IMU (SWIL: synthetic sweep) */
    imu_t imu = imu_swil_create(IMU_SWEEP_FRAMES);
    if (imu.init(&imu) != 0) {
        printf("[chango] FAIL - imu init\n");
        ao.shutdown(&ao);
        cam.shutdown(&cam);
        return 1;
    }

    /* Zero phase accumulators and filter state */
    memset(phases, 0, sizeof(phases));
    memset(lpf_state, 0, sizeof(lpf_state));

    int total_samples = 0;

    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        const uint8_t *image = cam.capture(&cam);
        if (!image) break;

        /* Scale source image (e.g., 320x240) → 256x256 */
        scale_image(image, cam.width, cam.height, scaled_image);

        /* Pixelate: 256x256 image → 4x4 intensity grid */
        pixelate(scaled_image, IMG_WIDTH,
                 REGION_W, REGION_H,
                 NUM_GRID_X, NUM_GRID_Y,
                 REGION_SHIFT, intensities);

        /* Read IMU and map to LPF parameters */
        imu_sample_t imu_sample;
        imu.read(&imu, &imu_sample);

        int lpf_cutoff_idx = ((int)imu_sample.accel_x + 1000) * (NUM_LPF_CUTOFFS - 1) / 2000;
        int lpf_q_idx = ((int)imu_sample.accel_y + 1000) * (NUM_LPF_RESONANCES - 1) / 2000;
        if (lpf_cutoff_idx < 0) lpf_cutoff_idx = 0;
        if (lpf_cutoff_idx >= NUM_LPF_CUTOFFS) lpf_cutoff_idx = NUM_LPF_CUTOFFS - 1;
        if (lpf_q_idx < 0) lpf_q_idx = 0;
        if (lpf_q_idx >= NUM_LPF_RESONANCES) lpf_q_idx = NUM_LPF_RESONANCES - 1;

        int lpf_offset = (lpf_cutoff_idx * NUM_LPF_RESONANCES + lpf_q_idx) * LPF_COEFFS_PER_ENTRY;
        int32_t lpf_b0 = lpf_coeffs[lpf_offset + 0];
        int32_t lpf_b1 = lpf_coeffs[lpf_offset + 1];
        int32_t lpf_b2 = lpf_coeffs[lpf_offset + 2];
        int32_t lpf_a1 = lpf_coeffs[lpf_offset + 3];
        int32_t lpf_a2 = lpf_coeffs[lpf_offset + 4];

        /* Synthesize: intensities → audio chunk */
        synthesize(intensities, NUM_TONES,
                   INV_NUM_TONES, INV_SHIFT,
                   sine_table, phase_incs,
                   phases, audio_buf, SAMPLES_PER_FRAME);

        /* Low-pass filter */
        lowpass(audio_buf, filtered_buf, SAMPLES_PER_FRAME,
                lpf_b0, lpf_b1, lpf_b2, lpf_a1, lpf_a2, lpf_state);

        /* Push filtered audio chunk to output */
        ao.write(&ao, filtered_buf, SAMPLES_PER_FRAME);

        total_samples += SAMPLES_PER_FRAME;
    }

    /* Shut down */
    imu.shutdown(&imu);
    ao.shutdown(&ao);
    cam.shutdown(&cam);

    return 0;
}
