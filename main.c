/*
 * main.c - Efficient E1x Chango test harness
 *
 * Runs the chango pipeline: image → pixelate → synthesize → audio output.
 * Writes raw 16-bit signed PCM to chango_output.raw.
 * Play with: play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw
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

/* Buffers */
uint8_t intensities[NUM_TONES];
uint32_t phases[NUM_TONES];
int16_t audio_out[NUM_AUDIO_SAMPLES];

#define OUTPUT_FILE "chango_output.raw"

int main() {
    /* Zero phase accumulators */
    memset(phases, 0, sizeof(phases));

    /* Prevent constant propagation of inputs */
    stop_propagation_u8((uint8_t *)test_image, IMG_WIDTH * IMG_HEIGHT);
    stop_propagation_i16((int16_t *)sine_table, SINE_TABLE_SIZE);

    /* Step 1: Pixelate - compute average intensity per region */
    enterProfileRegion("pixelate");
    pixelate(test_image, IMG_WIDTH, IMG_HEIGHT,
             NUM_GRID_X, NUM_GRID_Y, intensities);
    exitProfileRegion();

    /* Print intensity grid for debugging */
    printf("[chango] Intensity grid (%dx%d):\n", NUM_GRID_X, NUM_GRID_Y);
    for (int y = 0; y < NUM_GRID_Y; y++) {
        printf("  ");
        for (int x = 0; x < NUM_GRID_X; x++) {
            printf("%3d ", intensities[x * NUM_GRID_Y + y]);
        }
        printf("\n");
    }

    /* Step 2: Synthesize audio */
    enterProfileRegion("synthesize");
    synthesize(intensities, NUM_TONES, sine_table, phase_incs,
               phases, audio_out, NUM_AUDIO_SAMPLES);
    exitProfileRegion();

    /* Step 3: Write raw PCM to file (not stdout, to avoid stderr contamination) */
    FILE *fp = fopen(OUTPUT_FILE, "wb");
    if (!fp) {
        printf("[chango] FAIL - could not open %s\n", OUTPUT_FILE);
        return 1;
    }
    fwrite(audio_out, sizeof(int16_t), NUM_AUDIO_SAMPLES, fp);
    fclose(fp);

    printf("[chango] Wrote %s: %d samples (%d Hz, %.1f seconds)\n",
           OUTPUT_FILE, NUM_AUDIO_SAMPLES, SAMPLE_RATE,
           (float)NUM_AUDIO_SAMPLES / SAMPLE_RATE);
    printf("[chango] Play with: play -t raw -r %d -e signed -b 16 -c 1 %s\n",
           SAMPLE_RATE, OUTPUT_FILE);
    printf("[chango] PASS\n");

    return 0;
}
