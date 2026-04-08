/*
 * audio_out_swil.c - Software-in-the-loop audio output over UART
 *
 * Sends raw int16 little-endian PCM bytes directly to UART.
 * No text framing — the byte stream IS the audio.
 *
 * On hardware:
 *   cat /dev/ttyUSB0 | play -t raw -r 8000 -e signed -b 16 -c 1 -
 */

#include <stdio.h>
#include <stdint.h>
#include "audio_out.h"

#if defined(EFF_ARCH_E1X) && !defined(SIM_BUILD)
#include <eff.h>
#define UART_BYTE(b) eff_uart_putc(STDIO_UART, (char)(b))
#else
#define UART_BYTE(b) putchar((b))
#endif

typedef struct {
    int total_samples;
} audio_out_swil_ctx_t;

static audio_out_swil_ctx_t swil_ctx;

static int swil_init(audio_out_t *ao) {
    audio_out_swil_ctx_t *ctx = (audio_out_swil_ctx_t *)ao->ctx;
    ctx->total_samples = 0;
    return 0;
}

static int swil_write(audio_out_t *ao, const int16_t *samples, int num_samples) {
    audio_out_swil_ctx_t *ctx = (audio_out_swil_ctx_t *)ao->ctx;
    for (int i = 0; i < num_samples; i++) {
        uint16_t val = (uint16_t)samples[i];
        UART_BYTE(val & 0xFF);
        UART_BYTE((val >> 8) & 0xFF);
    }
    ctx->total_samples += num_samples;
    return 0;
}

static void swil_shutdown(audio_out_t *ao) {
    (void)ao;
}

audio_out_t audio_out_swil_create(const char *filename, int sample_rate) {
    (void)filename;
    swil_ctx.total_samples = 0;

    audio_out_t ao;
    ao.sample_rate = sample_rate;
    ao.init = swil_init;
    ao.write = swil_write;
    ao.shutdown = swil_shutdown;
    ao.ctx = &swil_ctx;
    return ao;
}
