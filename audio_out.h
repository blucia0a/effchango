/*
 * audio_out.h - Audio output interface for effchango
 *
 * Abstracts audio sample delivery so the pipeline can work with either
 * a software-in-the-loop file writer or a real hardware audio driver
 * (e.g., I2S DAC via DMA).
 *
 * Each implementation provides init/write/shutdown callbacks via the
 * audio_out_t struct. The pipeline calls write() each frame to push
 * a buffer of PCM samples to the output.
 */

#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdint.h>

typedef struct audio_out audio_out_t;

struct audio_out {
    int sample_rate;

    /* Initialize the audio output. Returns 0 on success. */
    int (*init)(audio_out_t *ao);

    /*
     * Write a buffer of samples to the output.
     * samples: array of 16-bit signed PCM
     * num_samples: number of samples in the buffer
     * Returns 0 on success.
     */
    int (*write)(audio_out_t *ao, const int16_t *samples, int num_samples);

    /* Flush any remaining data and release resources. */
    void (*shutdown)(audio_out_t *ao);

    /* Implementation-private context. */
    void *ctx;
};

/*
 * Software-in-the-loop audio output: writes raw PCM to a file.
 * No hardware required.
 *
 * filename: output file path (e.g., "chango_output.raw")
 * sample_rate: samples per second (stored for reference, not used by SWIL)
 */
audio_out_t audio_out_swil_create(const char *filename, int sample_rate);

#endif /* AUDIO_OUT_H */
