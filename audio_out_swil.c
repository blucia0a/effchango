/*
 * audio_out_swil.c - Software-in-the-loop audio output over printf/UART
 *
 * Prints samples as decimal integers over printf (routed to UART on E1x).
 * Framed with markers so a host-side script can extract the audio:
 *
 *   CHANGO_AUDIO_START <sample_rate>
 *   <sample0>
 *   <sample1>
 *   ...
 *   CHANGO_AUDIO_END
 *
 * On the host, capture UART output and run:
 *   python3 uart_to_raw.py < uart_log.txt | play -t raw -r 8000 -e signed -b 16 -c 1 -
 */

#include <stdio.h>
#include "audio_out.h"

typedef struct {
    int total_samples;
} audio_out_swil_ctx_t;

static audio_out_swil_ctx_t swil_ctx;

static int swil_init(audio_out_t *ao) {
    audio_out_swil_ctx_t *ctx = (audio_out_swil_ctx_t *)ao->ctx;
    ctx->total_samples = 0;
    printf("CHANGO_AUDIO_START %d\n", ao->sample_rate);
    return 0;
}

static int swil_write(audio_out_t *ao, const int16_t *samples, int num_samples) {
    audio_out_swil_ctx_t *ctx = (audio_out_swil_ctx_t *)ao->ctx;
    for (int i = 0; i < num_samples; i++) {
        printf("%d\n", (int)samples[i]);
    }
    ctx->total_samples += num_samples;
    return 0;
}

static void swil_shutdown(audio_out_t *ao) {
    (void)ao;
    printf("CHANGO_AUDIO_END\n");
}

audio_out_t audio_out_swil_create(const char *filename, int sample_rate) {
    (void)filename; /* not used — output goes to printf/UART */
    swil_ctx.total_samples = 0;

    audio_out_t ao;
    ao.sample_rate = sample_rate;
    ao.init = swil_init;
    ao.write = swil_write;
    ao.shutdown = swil_shutdown;
    ao.ctx = &swil_ctx;
    return ao;
}
