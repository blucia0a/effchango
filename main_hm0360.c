/*
 * main_hm0360.c - effchango on real hardware: HM0360 camera -> chango -> UART
 *
 * The camera bring-up and capture handshake in here are taken verbatim from the
 * known-good minimal-camera demo (sales_demo_kit branch kit-dash-polish,
 * demos/minimal_camera/main.c), which is the reference that streams real frames
 * on this EVK. Call order, register tables, pin map, clock boost, UART divisors
 * and the VSYNC handshake are all deliberately unchanged from it -- resist
 * "tidying" them, because that ordering is the part that works.
 *
 * The only change is what happens with a captured frame. Where the demo emits
 * the raw pixels, this runs the chango pipeline:
 *
 *   capture -> scale_image(256x256) -> pixelate(4x4) -> synthesize(16 tones)
 *           -> lowpass() -> 8-bit unsigned PCM out over the same UART
 *
 * Build CAMERA_DEBUG=1 to emit raw frames the demo's way instead, for checking
 * the camera with view.py / grab_frames.py.
 *
 * The SWIL build (no HW_CAMERA) uses main.c and the camera_t abstraction
 * instead; this file is the hardware path only.
 */

#include <eff.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "hm0360.h"

#define DEFINE_CHANGO_DATA
#include <chango_data.h.inc>

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

/* ---- verbatim from minimal-camera ------------------------------------- */

#define CAM_I2C_PINMUX  PINMUX_2
#define CAM_I2C_MODULE  I2C_2_1

#define CAM_I2C_ADDR    0x24

#define CAM_CTL_PINMUX  PINMUX_6
#define CAM_CTL_GPIO    GPIO_6

#define CAM_XSHUTD_PIN  GPIO_PIN_5
#define CAM_VSYNC_PIN   GPIO_PIN_4
#define CAM_OSC_EN_PIN  GPIO_PIN_3
#define CAM_XSLEEP_PIN  GPIO_PIN_2
#define CAM_LDO_EN_PIN  GPIO_PIN_1
#define CAM_MCLK_PIN    GPIO_PIN_0

/* QQVGA is what every hardware-validated demo in the kit runs, and chango
 * averages the frame into a 4x4 grid regardless, so the larger modes only cost
 * SPI readout time. */
#define HIMAX_QQVGA
//#define HIMAX_QVGA
//#define HIMAX_VGA

uint32_t* pmu_clock_control = (uint32_t*)0x400310;

/* MODE_SELECT (0x0100) [2:0] values, per datasheet 10.2. */
#define HM0360_MODE_SLEEP      0x00
#define HM0360_MODE_STREAMING  0x01   /* continuous, SW I2C trigger */
#define HM0360_MODE_SNAPSHOT   0x03   /* snapshot with N frames output */

/*
 * Capture mode. Streaming is the default: the sensor free-runs, so every VSYNC
 * sync lands on a fresh frame. Set CAM_STREAMING=0 for the donor's snapshot
 * path, which is retained mainly for comparison.
 */
#ifndef CAM_STREAMING
#define CAM_STREAMING 1
#endif

/* ---- chango wiring ---------------------------------------------------- */

/*
 * Audio samples generated per captured frame.
 *
 * This sets the ratio of per-sample synthesis work to the fixed per-frame cost
 * (snapshot capture + scale + pixelate), and so it is the main knob for the
 * achieved sample rate: raising it amortizes the fixed cost over more samples.
 * It also sets how often the image can change the sound -- one image controls
 * SAMPLES_PER_FRAME / SAMPLE_RATE seconds of audio.
 *
 * 1344 was picked by measurement, not derivation: in streaming mode on this EVK
 * at QQVGA it lands the stream on ~7954 Hz, 0.6% under the 8000 Hz the tone
 * table assumes, which is -0.10 semitones and inaudible. One image then drives
 * 169 ms of audio, about 6 updates/second.
 *
 * This value is specific to CAM_STREAMING. The snapshot path is much slower
 * (its trigger->VSYNC latency alone is tens of ms) and needed 1920 to reach the
 * same rate, at only ~4 updates/second. Changing capture mode, resolution or the
 * kernels means retuning: `make fabric HW_CAMERA=1 SAMPLES_PER_FRAME=N` plus
 * `listen_audio.py --measure`.
 */
#ifndef SAMPLES_PER_FRAME
#define SAMPLES_PER_FRAME 1344
#endif

/*
 * Precomputed to keep division out of the kernels.
 *   pixelate:   256x256 image / 4x4 grid = 64x64 regions = 4096 px
 *   synthesize: 16 tones is a power of 2, so averaging is a pure shift
 */
#define REGION_W (IMG_WIDTH / NUM_GRID_X)   /* 64 */
#define REGION_H (IMG_HEIGHT / NUM_GRID_Y)  /* 64 */
#define REGION_SHIFT 12  /* log2(64 * 64) */

#define INV_NUM_TONES 1
#define INV_SHIFT 4      /* log2(16) */

/*
 * IMU -> low-pass modulation.
 *
 * There is no IMU part wired up on this hat, so imu_swil.c fabricates the tilt
 * on-device as a triangle wave. That produced a continuous filter sweep whether
 * or not anything was moving, which is a synthetic effect rather than a response
 * to the world, so it is off by default.
 *
 * With USE_IMU 0 the biquad still runs -- it is part of the chango voice -- but
 * at a fixed cutoff and Q, so the only thing shaping the sound is the image.
 * Build with IMU=1 to get the sweep back.
 */
#ifndef USE_IMU
#define USE_IMU 0
#endif

/* IMU tilt sweep period in frames, when USE_IMU is on. */
#define IMU_SWEEP_FRAMES 128

/*
 * Fixed filter setting used when USE_IMU is 0. Cutoffs are 16 log-spaced steps
 * over 200-3800 Hz and Q is 8 log-spaced steps over 0.5-8.0 (see gen_data.py).
 *
 * DO NOT raise these blindly. lowpass() accumulates into an int32_t, and only
 * 19 of the 128 (cutoff, Q) entries in lpf_coeffs have small enough coefficients
 * to stay inside it -- see "Biquad int32 overflow" in CLAUDE.md. Picking an
 * unsafe entry makes the filter output overflow into loud garbage that barely
 * responds to the image at all.
 *
 * cutoff_idx 12 (2109 Hz) with q_idx 0 (Q=0.50) is the highest-cutoff safe
 * entry, with 1.70x headroom. It passes the whole tone table, which tops out at
 * 1568 Hz, and has no resonant peak to clip against.
 */
#ifndef LPF_CUTOFF_IDX
#define LPF_CUTOFF_IDX 12
#endif
#ifndef LPF_Q_IDX
#define LPF_Q_IDX 0
#endif

#if LPF_CUTOFF_IDX < 0 || LPF_CUTOFF_IDX >= NUM_LPF_CUTOFFS
#error "LPF_CUTOFF_IDX out of range"
#endif
#if LPF_Q_IDX < 0 || LPF_Q_IDX >= NUM_LPF_RESONANCES
#error "LPF_Q_IDX out of range"
#endif

/* Statics, not locals: scaled_image alone is 64 KB and would swamp the stack. */
static uint8_t scaled_image[IMG_WIDTH * IMG_HEIGHT];
static uint8_t intensities[NUM_TONES];
static uint32_t phases[NUM_TONES];
static int16_t audio_buf[SAMPLES_PER_FRAME];
static int16_t filtered_buf[SAMPLES_PER_FRAME];
static int32_t lpf_state[4];   /* {x1, x2, y1, y2} */

int main() {
    // Init camera control pins
    eff_pinmux_set(CAM_CTL_PINMUX, PINMUX_GPIO);
    eff_gpio_dir_set(CAM_CTL_GPIO, (CAM_XSHUTD_PIN |
                                    CAM_OSC_EN_PIN |
                                    CAM_XSLEEP_PIN |
                                    CAM_LDO_EN_PIN), EFF_GPIO_OUT);
    eff_gpio_dir_set(CAM_CTL_GPIO, CAM_VSYNC_PIN, EFF_GPIO_IN);
    eff_gpio_pull_set(CAM_CTL_GPIO, CAM_VSYNC_PIN, EFF_GPIO_PULL_NONE);

    // Camera power-on sequence

    // Ensure all camera components off
    eff_gpio_clear(CAM_CTL_GPIO, CAM_OSC_EN_PIN |
                                 CAM_XSLEEP_PIN |
                                 CAM_XSHUTD_PIN);
    sleep_ms(1);

    // Enable camera oscillator input
    eff_gpio_set(CAM_CTL_GPIO, CAM_OSC_EN_PIN);
    sleep_ms(1);

    // Power on camera via LDOs
    eff_gpio_set(CAM_CTL_GPIO, CAM_LDO_EN_PIN);
    sleep_ms(1);

    // Exit sleep
    eff_gpio_set(CAM_CTL_GPIO, CAM_XSLEEP_PIN);
    sleep_ms(1);

    // Exit shutdown
    eff_gpio_set(CAM_CTL_GPIO, CAM_XSHUTD_PIN);
    sleep_ms(1);

    // Init camera I2C
    eff_pinmux_set(CAM_I2C_PINMUX, PINMUX_SPI_I2C1);
    if(eff_i2c_init(CAM_I2C_MODULE, I2C_SPEED_400K) != 0) {
        printf("Camera I2C init failed\r\n");
    }

    // Profiling
    eff_pinmux_set(PINMUX_11, PINMUX_GPIO);
    eff_gpio_dir_set(GPIO_11, GPIO_PIN_2, EFF_GPIO_OUT);
    eff_gpio_clear(GPIO_11, GPIO_PIN_2);

    // Abuse UART to make 929370 (nominal 921600) baud
    *pmu_clock_control = 0x0201;
    eff_uart_cfg_t uart_cfg = EFF_UART_DEFAULTS;
    uart_cfg.oscr = 0xE;
    uart_cfg.baud = 558080 / 3;
    eff_uart_init(STDIO_UART, uart_cfg);

    uint16_t model_id = 0;
    hm0360_read_reg(MODEL_ID_H, ((uint8_t*) &model_id + 1));
    hm0360_read_reg(MODEL_ID_L, ((uint8_t*) &model_id));
    if(model_id != HM0360_MODEL_ID) {
        printf("Camera model ID check failed: got 0x%X expected 0x%X\r\n", model_id, HM0360_MODEL_ID);
        while(1);
    }

    for(size_t i = 0; hm0360_base_config[i][0] != 0x0000; i++) {
        hm0360_write_reg(hm0360_base_config[i][0], hm0360_base_config[i][1]);
    }

    #ifdef HIMAX_QQVGA
    #define FRAME_W 160
    #define FRAME_H 120
    for(size_t i = 0; himax_qqvga_regs[i][0] != 0x0000; i++) {
        hm0360_write_reg(himax_qqvga_regs[i][0], himax_qqvga_regs[i][1]);
    }
    #endif

    #ifdef HIMAX_QVGA
    #define FRAME_W 320
    #define FRAME_H 240
    for(size_t i = 0; himax_qvga_regs[i][0] != 0x0000; i++) {
        hm0360_write_reg(himax_qvga_regs[i][0], himax_qvga_regs[i][1]);
    }
    #endif

    #ifdef HIMAX_VGA
    #define FRAME_W 640
    #define FRAME_H 480
    for(size_t i = 0; himax_vga_regs[i][0] != 0x0000; i++) {
        hm0360_write_reg(himax_vga_regs[i][0], himax_vga_regs[i][1]);
    }
    #endif

#ifdef CAM_TEST_PATTERN
    /*
     * Bring-up diagnostic: replace the pixel array output with the sensor's own
     * pattern generator. TEST_PATTERN_MODE 0x0601 is [6:4]=mode, [0]=enable, so
     * 0x01 is Colour Bar (vertical grey bars on this mono part) and 0x21 is
     * Walking 1's. Written after both register tables so it wins, then latched.
     *
     * If the frames we receive do NOT turn into bars, the data reaching us is
     * not the sensor's current output at all -- which separates a capture or
     * exposure fault from a dead/stale readout path.
     */
    hm0360_write_reg(TEST_PATTERN_MODE, CAM_TEST_PATTERN);
    hm0360_write_reg(COMMAND_UPDATE, 0x01);
#endif

    eff_pinmux_set(PINMUX_0, PINMUX_SPI);
    eff_spi_cfg_t spi_cfg = EFF_SPI_DEFAULTS;
    spi_cfg.xfer_mode = SPI_XFER_READ_ONLY;
    spi_cfg.bus_size = SPI_BUS_QUAD;
    spi_cfg.mode = SPI_MODE_1;
    spi_cfg.is_slave = 1;

    eff_spi_init(SPI_0, &spi_cfg);

    static uint8_t frame[(FRAME_W * FRAME_H)] = {0};
    uint8_t magic[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    (void)magic;

    /* ---- chango setup, before the sensor is armed ---- */

    audio_out_t ao = audio_out_swil_create("chango_output.raw", SAMPLE_RATE);
    ao.init(&ao);

#if USE_IMU
    imu_t imu = imu_swil_create(IMU_SWEEP_FRAMES);
    imu.init(&imu);
#endif

    memset(phases, 0, sizeof(phases));
    memset(lpf_state, 0, sizeof(lpf_state));

    /*
     * Arm the sensor. MODE_SELECT (0x0100) [2:0] selects a *state*, not a pulse:
     *   000 sleep, 001 continuous streaming, 011 snapshot with N frames output.
     *
     * The donor demo sits in snapshot mode and rewrites 0x03 every iteration.
     * Only the first of those is a 000 -> 011 transition; every later write is
     * 011 -> 011, which the state machine ignores, so the sensor never
     * re-exposes and the readout keeps returning the same frame forever. That is
     * the "camera works but the audio never responds" fault, and it is why
     * covering the lens changed nothing.
     *
     * Streaming mode avoids the whole re-arm problem: the sensor free-runs and
     * emits frames back to back, so syncing to a VSYNC edge always lands on
     * fresh pixels. Frames that go by while we synthesize are simply skipped.
     * (The kit reached the same conclusion independently -- RESULTS-CAM-020.)
     */
#if CAM_STREAMING
    hm0360_write_reg(MODE_SELECT, HM0360_MODE_STREAMING);
    hm0360_write_reg(COMMAND_UPDATE, 0x01);
#else
    hm0360_write_reg(MODE_SELECT, HM0360_MODE_SLEEP);
#endif

    while(1) {
#if !CAM_STREAMING
        /* Snapshot: return to sleep first so the next write is a real
         * transition, otherwise the trigger is silently ignored. */
        hm0360_write_reg(MODE_SELECT, HM0360_MODE_SLEEP);
        hm0360_write_reg(MODE_SELECT, HM0360_MODE_SNAPSHOT);
#endif

        // Wait for posedge on VSYNC to capture one whole frame
        while(eff_gpio_get(CAM_CTL_GPIO, CAM_VSYNC_PIN));   // block while high
        while(!eff_gpio_get(CAM_CTL_GPIO, CAM_VSYNC_PIN));  // block while low

        eff_gpio_set(GPIO_11, GPIO_PIN_2);
        for(int row = FRAME_H - 1; row >= 0; row--) {   // Flip frame
            eff_spi_xfer(SPI_0, 0, 0, NULL, 0,
                         frame + (row * FRAME_W),
                         sizeof(frame[0]) * FRAME_W);
        }
        eff_gpio_clear(GPIO_11, GPIO_PIN_2);

#ifdef CAMERA_DEBUG
        /* Camera check: emit raw frames exactly as the demo does. */
        for(size_t i = 0; i < sizeof(magic); i++) {
            eff_uart_putc(STDIO_UART, magic[i]);
        }
        for(size_t i = 0; i < FRAME_H * FRAME_W; i++) {
            eff_uart_putc(STDIO_UART, frame[i]);
        }
#elif defined(INTENSITY_DEBUG)
        /*
         * Diagnostic: emit just the 4x4 intensity grid that synthesize() is fed,
         * so it can be watched changing (or not) as the scene changes. This taps
         * the pipeline between pixelate and synthesize, which is where a
         * "camera works but audio does not respond" fault has to live.
         * Host side: diag_intensity.py.
         */
        scale_image(frame, FRAME_W, FRAME_H, scaled_image);
        pixelate(scaled_image, IMG_WIDTH,
                 REGION_W, REGION_H,
                 NUM_GRID_X, NUM_GRID_Y,
                 REGION_SHIFT, intensities);

        eff_uart_putc(STDIO_UART, 0xAB);
        eff_uart_putc(STDIO_UART, 0xCD);
        eff_uart_putc(STDIO_UART, 0xEF);
        eff_uart_putc(STDIO_UART, 0x01);
        for (int i = 0; i < NUM_TONES; i++) {
            eff_uart_putc(STDIO_UART, intensities[i]);
        }
        /* Also send a couple of raw frame samples, to tell "the sensor buffer is
         * static" apart from "the kernels are not rerunning". */
        eff_uart_putc(STDIO_UART, frame[0]);
        eff_uart_putc(STDIO_UART, frame[(FRAME_H / 2) * FRAME_W + FRAME_W / 2]);
        eff_uart_putc(STDIO_UART, scaled_image[0]);
        eff_uart_putc(STDIO_UART, scaled_image[128 * IMG_WIDTH + 128]);
#else
        /* Camera frame -> audio. */

        /* Scale the sensor frame to 256x256 for a power-of-2 grid. */
        scale_image(frame, FRAME_W, FRAME_H, scaled_image);

        /* Average down to a 4x4 grid of intensities, one per tone. */
        pixelate(scaled_image, IMG_WIDTH,
                 REGION_W, REGION_H,
                 NUM_GRID_X, NUM_GRID_Y,
                 REGION_SHIFT, intensities);

#if USE_IMU
        /* Tilt drives the low-pass filter. */
        imu_sample_t imu_sample;
        imu.read(&imu, &imu_sample);

        int lpf_cutoff_idx = ((int)imu_sample.accel_x + 1000) * (NUM_LPF_CUTOFFS - 1) / 2000;
        int lpf_q_idx = ((int)imu_sample.accel_y + 1000) * (NUM_LPF_RESONANCES - 1) / 2000;
        if (lpf_cutoff_idx < 0) lpf_cutoff_idx = 0;
        if (lpf_cutoff_idx >= NUM_LPF_CUTOFFS) lpf_cutoff_idx = NUM_LPF_CUTOFFS - 1;
        if (lpf_q_idx < 0) lpf_q_idx = 0;
        if (lpf_q_idx >= NUM_LPF_RESONANCES) lpf_q_idx = NUM_LPF_RESONANCES - 1;
#else
        /* Fixed filter: the image is the only thing shaping the sound. */
        const int lpf_cutoff_idx = LPF_CUTOFF_IDX;
        const int lpf_q_idx = LPF_Q_IDX;
#endif

        int lpf_offset = (lpf_cutoff_idx * NUM_LPF_RESONANCES + lpf_q_idx) * LPF_COEFFS_PER_ENTRY;

        /* One tone per region, amplitude = region intensity. */
        synthesize(intensities, NUM_TONES,
                   INV_NUM_TONES, INV_SHIFT,
                   sine_table, phase_incs,
                   phases, audio_buf, SAMPLES_PER_FRAME);

        lowpass(audio_buf, filtered_buf, SAMPLES_PER_FRAME,
                lpf_coeffs[lpf_offset + 0], lpf_coeffs[lpf_offset + 1],
                lpf_coeffs[lpf_offset + 2], lpf_coeffs[lpf_offset + 3],
                lpf_coeffs[lpf_offset + 4], lpf_state);

        /* 8-bit unsigned PCM out over the same UART. */
        ao.write(&ao, filtered_buf, SAMPLES_PER_FRAME);
#endif
    }
}
