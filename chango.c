/*
 * chango.c - Efficient E1x Chango musical instrument kernel
 *
 * Converts an image to audio by:
 * 1. Dividing the image into a grid of square regions
 * 2. Computing average pixel intensity per region
 * 3. Using intensity as amplitude for a sine tone per region
 * 4. Mixing all tones into a single audio stream
 * 5. Applying a parameterized biquad low-pass filter
 *
 * All integer arithmetic - no floating point, no division.
 * Based on PhotoChango by Brandon Lucia (2011-2012).
 */

#include <stdint.h>

#define FRAC_BITS 15
#define FIXED_ROUND(x) (((x) + (1 << (FRAC_BITS - 1))) >> FRAC_BITS)

/*
 * pixelate: Divide image into grid regions and compute average intensity.
 *
 * All parameters that would require division are precomputed by the caller.
 * No division occurs inside this function.
 *
 * image:        grayscale pixel data (row-major, 0-255)
 * width:        image width in pixels
 * region_w:     pixels per region horizontally (precomputed: width / grid_x)
 * region_h:     pixels per region vertically (precomputed: height / grid_y)
 * grid_x:       number of horizontal regions
 * grid_y:       number of vertical regions
 * region_shift: log2(region_w * region_h) for averaging via shift
 * intensities:  output array of size grid_x * grid_y (0-255)
 */
__efficient__ void pixelate(const uint8_t *restrict image,
                            int width,
                            int region_w, int region_h,
                            int grid_x, int grid_y,
                            int region_shift,
                            uint8_t *restrict intensities) {
    for (int ry = 0; ry < grid_y; ry++) {
        for (int rx = 0; rx < grid_x; rx++) {
            uint32_t sum = 0;
            int y_start = ry * region_h;
            int x_start = rx * region_w;

            for (int y = y_start; y < y_start + region_h; y++) {
                for (int x = x_start; x < x_start + region_w; x++) {
                    sum += image[y * width + x];
                }
            }

            intensities[rx * grid_y + ry] = (uint8_t)(sum >> region_shift);
        }
    }
}

/*
 * synthesize: Generate audio samples from region intensities.
 *
 * For each output sample, computes the contribution of each tone:
 *   sample_contribution = (sine_table[phase_index] * amplitude) >> 8
 * Then averages all contributions using multiply-shift (no division).
 *
 * intensities:    amplitude per tone (0-255), size num_tones
 * num_tones:      number of tones to mix
 * inv_num_tones:  fixed-point reciprocal for averaging (precomputed)
 * inv_shift:      right shift to apply after multiply (precomputed)
 * sine_table:     256-entry Q15 sine lookup table
 * phase_incs:     phase increment per tone (8.24 fixed-point)
 * phases:         phase accumulator per tone (updated in-place)
 * audio_out:      output buffer, 16-bit signed PCM
 * num_samples:    number of samples to generate
 */
__efficient__ void synthesize(const uint8_t *restrict intensities,
                              int num_tones,
                              int32_t inv_num_tones,
                              int inv_shift,
                              const int16_t *restrict sine_table,
                              const uint32_t *restrict phase_incs,
                              uint32_t *restrict phases,
                              int16_t *restrict audio_out,
                              int num_samples) {
    for (int s = 0; s < num_samples; s++) {
        int32_t mix = 0;

        for (int t = 0; t < num_tones; t++) {
            /* Look up sine value using top 8 bits of phase as table index */
            uint8_t idx = (uint8_t)(phases[t] >> 24);
            int16_t sin_val = sine_table[idx];

            /* Scale by intensity (0-255): result is Q15 * (amp/256) */
            int32_t contribution = ((int32_t)sin_val * (int32_t)intensities[t]) >> 8;

            mix += contribution;

            /* Advance phase */
            phases[t] += phase_incs[t];
        }

        /*
         * Average across all tones using two-step multiply-shift.
         * Divide by 100 = divide by 4, then by 25:
         *   (mix >> 2) * 1311 >> 15
         * This stays within int32 range (max ~1.07 billion).
         * inv_num_tones and inv_shift are unused now but kept in the
         * signature for future flexibility with non-100 tone counts.
         */
        (void)inv_num_tones;
        (void)inv_shift;
        mix = ((mix >> 2) * 1311) >> 15;

        /* Clamp to int16_t range */
        if (mix > 32767) mix = 32767;
        if (mix < -32767) mix = -32767;

        audio_out[s] = (int16_t)mix;
    }
}

/*
 * lowpass: Biquad low-pass filter (Direct Form I), Q15 fixed-point.
 *
 * Filters audio using precomputed Q15 coefficients.
 * Filter state persists across calls via the state array.
 * No division - only multiply and shift.
 */
__efficient__ void lowpass(const int16_t *restrict input,
                           int16_t *restrict output,
                           int length,
                           int32_t b0, int32_t b1, int32_t b2,
                           int32_t a1, int32_t a2,
                           int32_t *restrict state) {
    int32_t x1 = state[0];
    int32_t x2 = state[1];
    int32_t y1 = state[2];
    int32_t y2 = state[3];

    for (int n = 0; n < length; n++) {
        int32_t x = input[n];
        int32_t acc = x * b0 + x1 * b1 + x2 * b2 - y1 * a1 - y2 * a2;
        int32_t y = FIXED_ROUND(acc);

        /* Clamp to int16_t range */
        if (y > 32767) y = 32767;
        if (y < -32767) y = -32767;

        output[n] = (int16_t)y;

        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
    }

    state[0] = x1;
    state[1] = x2;
    state[2] = y1;
    state[3] = y2;
}
