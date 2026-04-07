/*
 * main.c - Efficient E1x Chango main loop
 *
 * Runs the chango pipeline in a frame loop:
 *   camera.capture() → pixelate() → synthesize() → audio_out.write()
 *
 * Camera and audio output are abstracted behind interfaces (camera.h,
 * audio_out.h) so hardware drivers can be swapped in later. Currently
 * uses software-in-the-loop implementations (test image + file output).
 *
 * Play output with:
 *   play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <test_common.h>

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

/* Kernel declarations */
void pixelate(const uint8_t *restrict image,
              int width, int height,
              int grid_x, int grid_y,
              uint8_t *restrict intensities);

void synthesize(const uint8_t *restrict intensities,
                int num_tones,
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

/*
 * Audio samples per frame. At 8000 Hz sample rate and ~30 fps,
 * each frame produces ~267 samples. We use 256 for alignment.
 */
#define SAMPLES_PER_FRAME 256

/* Total frames to render (SWIL mode). 2 seconds = 8000/256 ≈ 31 frames. */
#define NUM_FRAMES (NUM_AUDIO_SAMPLES / SAMPLES_PER_FRAME)

/*
 * Default LPF settings. Cutoff index 10/15 ≈ mid-high, Q index 2/7 ≈ moderate.
 * These can be overridden at runtime by sensors or other control signals.
 */
#define DEFAULT_LPF_CUTOFF_IDX 10
#define DEFAULT_LPF_Q_IDX 2

/* Buffers */
uint8_t intensities[NUM_TONES];
uint32_t phases[NUM_TONES];
int16_t audio_buf[SAMPLES_PER_FRAME];
int16_t filtered_buf[SAMPLES_PER_FRAME];
int32_t lpf_state[4]; /* {x1, x2, y1, y2} */

#define OUTPUT_FILE "chango_output.raw"

int main() {
    /* Prevent constant propagation of inputs */
    stop_propagation_u8((uint8_t *)test_image, IMG_WIDTH * IMG_HEIGHT);
    stop_propagation_i16((int16_t *)sine_table, SINE_TABLE_SIZE);

    /* Set up camera (software-in-the-loop: returns test image) */
    camera_t cam = camera_swil_create(test_image, IMG_WIDTH, IMG_HEIGHT);
    if (cam.init(&cam) != 0) {
        printf("[chango] FAIL - camera init\n");
        return 1;
    }

    /* Set up audio output (software-in-the-loop: writes to file) */
    audio_out_t ao = audio_out_swil_create(OUTPUT_FILE, SAMPLE_RATE);
    if (ao.init(&ao) != 0) {
        printf("[chango] FAIL - audio output init\n");
        cam.shutdown(&cam);
        return 1;
    }

    /* Zero phase accumulators and filter state */
    memset(phases, 0, sizeof(phases));
    memset(lpf_state, 0, sizeof(lpf_state));

    /* Look up LPF coefficients from precomputed table */
    int lpf_cutoff_idx = DEFAULT_LPF_CUTOFF_IDX;
    int lpf_q_idx = DEFAULT_LPF_Q_IDX;
    int lpf_offset = (lpf_cutoff_idx * NUM_LPF_RESONANCES + lpf_q_idx) * LPF_COEFFS_PER_ENTRY;
    int32_t lpf_b0 = lpf_coeffs[lpf_offset + 0];
    int32_t lpf_b1 = lpf_coeffs[lpf_offset + 1];
    int32_t lpf_b2 = lpf_coeffs[lpf_offset + 2];
    int32_t lpf_a1 = lpf_coeffs[lpf_offset + 3];
    int32_t lpf_a2 = lpf_coeffs[lpf_offset + 4];

    printf("[chango] Starting: %d frames, %d samples/frame, %d Hz\n",
           NUM_FRAMES, SAMPLES_PER_FRAME, SAMPLE_RATE);
    printf("[chango] LPF: cutoff_idx=%d, q_idx=%d, coeffs=[%d,%d,%d,%d,%d]\n",
           lpf_cutoff_idx, lpf_q_idx, lpf_b0, lpf_b1, lpf_b2, lpf_a1, lpf_a2);

    int total_samples = 0;

    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        /* Capture a frame */
        const uint8_t *image = cam.capture(&cam);
        if (!image) {
            printf("[chango] FAIL - camera capture at frame %d\n", frame);
            break;
        }

        /* Pixelate: image → intensity grid */
        enterProfileRegion("pixelate");
        pixelate(image, cam.width, cam.height,
                 NUM_GRID_X, NUM_GRID_Y, intensities);
        exitProfileRegion();

        /* Print intensity grid on first frame */
        if (frame == 0) {
            printf("[chango] Intensity grid (%dx%d):\n", NUM_GRID_X, NUM_GRID_Y);
            for (int y = 0; y < NUM_GRID_Y; y++) {
                printf("  ");
                for (int x = 0; x < NUM_GRID_X; x++) {
                    printf("%3d ", intensities[x * NUM_GRID_Y + y]);
                }
                printf("\n");
            }
        }

        /* Synthesize: intensities → audio chunk (phases persist across frames) */
        enterProfileRegion("synthesize");
        synthesize(intensities, NUM_TONES, sine_table, phase_incs,
                   phases, audio_buf, SAMPLES_PER_FRAME);
        exitProfileRegion();

        /* Low-pass filter (state persists across frames) */
        enterProfileRegion("lowpass");
        lowpass(audio_buf, filtered_buf, SAMPLES_PER_FRAME,
                lpf_b0, lpf_b1, lpf_b2, lpf_a1, lpf_a2, lpf_state);
        exitProfileRegion();

        /* Push filtered audio chunk to output */
        if (ao.write(&ao, filtered_buf, SAMPLES_PER_FRAME) != 0) {
            printf("[chango] FAIL - audio write at frame %d\n", frame);
            break;
        }

        total_samples += SAMPLES_PER_FRAME;
    }

    /* Shut down */
    ao.shutdown(&ao);
    cam.shutdown(&cam);

    printf("[chango] Wrote %s: %d samples (%d Hz, %.1f seconds)\n",
           OUTPUT_FILE, total_samples, SAMPLE_RATE,
           (float)total_samples / SAMPLE_RATE);
    printf("[chango] Play with: play -t raw -r %d -e signed -b 16 -c 1 %s\n",
           SAMPLE_RATE, OUTPUT_FILE);
    printf("[chango] PASS\n");

    return 0;
}
