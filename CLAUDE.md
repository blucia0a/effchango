# effchango — Claude Context

This file gives Claude the full context needed to continue working on this project.

## What this project is

effchango is a port of the **Chango** musical instrument to the **Efficient E1x** embedded processor. The original Chango (created by Brandon Lucia, GitHub: blucia0a, 2010-2012) converts camera images to audio. This version targets an integer-only embedded platform with no floating point.

Original repos for reference:
- https://github.com/blucia0a/PhotoChango — the image-to-audio pipeline this is based on
- https://github.com/blucia0a/Chango — the original mouse-based instrument
- https://github.com/blucia0a/chango2.0 — expanded version with MIDI, beat detection, etc.

## Architecture

### Pipeline (runs as a frame loop in main.c)

```
camera.capture(320x240) → scale_image(256x256) → pixelate(4x4) → synthesize(16 tones) → lowpass() → audio_out.write()
                                                                        ↑                     ↑
                                                                   sine table            IMU controls
                                                                   phase incs            cutoff/resonance
```

Each frame: capture a QVGA image (320×240), nearest-neighbor scale to 256×256, divide into 4×4 grid (64×64 pixel regions), compute average intensity per region, generate 512 audio samples (one tone per region, amplitude = intensity), apply biquad low-pass filter, send to UART as 8-bit unsigned PCM.

### Key design constraints

- **No floating point.** E1x has no FPU. All arithmetic is integer. Sine waves use a 256-entry Q15 lookup table. Filter coefficients are Q15 (int32_t scaled by 32767). Phase accumulators are uint32_t.
- **No division on the fabric.** Division hangs or runs extremely slowly on the E1x fabric. All division is replaced with precomputed multiply-shift approximations or bit shifts. Region averaging uses `>> region_shift` (power-of-2 region sizes). Tone averaging uses `(mix * inv_num_tones) >> inv_shift`.
- **No filesystem.** E1x is embedded. Audio output goes over UART as raw binary bytes.
- **SWIL pattern.** All hardware interfaces (camera, audio output, IMU) use a function-pointer struct pattern (`camera_t`, `audio_out_t`, `imu_t`). Each has a software-in-the-loop (SWIL) implementation for testing without hardware. When hardware arrives, write a new `_hw.c` implementing the same interface and swap the `_create()` call in main.c.

### Files

| File | Role |
|------|------|
| `chango.c` | `__efficient__` kernels: `scale_image()`, `pixelate()`, `synthesize()`, `lowpass()` |
| `main.c` | Frame loop wiring all the pieces together |
| `camera.h` / `camera_swil.c` | Camera interface + SWIL (returns embedded test image) |
| `audio_out.h` / `audio_out_swil.c` | Audio output interface + SWIL (raw 8-bit PCM bytes over UART) |
| `imu.h` / `imu_swil.c` | IMU interface + SWIL (triangle-wave accel sweep) |
| `uart_to_raw.py` | Host-side: decodes text-mode UART output (legacy, not used with binary mode) |
| `gen_data.py` | Generates `chango_data.h.inc`: test image, sine table, phase increments, LPF coefficient table |
| `CMakeLists.txt` | For integration into the Efficient apps build tree (`add_eff_app`) |
| `Makefile` | Standalone build with `effcc` |

### Generated data (`chango_data.h.inc`, via `gen_data.py`)

- 320×240 QVGA grayscale test image (synthetic gradient + circle pattern)
- 256-entry Q15 sine lookup table
- 16 phase increments (one per tone, for 4×4 grid)
- Biquad LPF coefficient table: 16 cutoff frequencies (200-3800 Hz, log-spaced) × 8 Q values (0.5-8.0, log-spaced) × 5 coefficients (b0, b1, b2, a1, a2) in Q15

### Audio format

- **8-bit unsigned PCM**, mono, 8000 Hz sample rate
- Converted from internal 16-bit signed by taking high byte + 128: `(sample >> 8) + 128`
- 1 byte per sample at 115200 baud = 11,520 samples/sec (44% headroom over 8kHz)
- 512 samples per frame
- On hardware: `cat /dev/ttyUSB0 | play -t raw -r 8000 -e unsigned -b 8 -c 1 -`

## Building

Requires `effcc` from the Efficient Computer toolchain. Activate the venv first:

```bash
source ~/venv/bin/activate
```

### SDK build (one-time setup)

The fabric build links against `libeff.a` from the SDK, built with UART3 for stdio:

```bash
cd sdk_build
cmake . -DEFF_ARCH=e1x -DEFFTOOLS_DIR=/home/blucia/venv \
        -DEFF_STDIO_PORT=3 -DEFF_SUBTARGETS="scalar;fabric" -DEFF_PNRVIZ_SVGS=0
make eff
```

### Sim build (functional testing on host)

```bash
make sim
```

Note: sim builds with `--sim` flag. The sim backend is very slow with manually unrolled `__efficient__` functions — keep kernels simple for sim testing. The sim does NOT link the SDK (no UART driver); audio goes through putchar/stdout.

### Fabric build (for E1x hardware)

```bash
make fabric    # produces chango_fabric + chango_fabric.hex
```

Compiles with `-O3 -flto --target=e1x -DUSE_PRAGMAS`, links against SDK `libeff.a` with `--whole-archive`, converts to Verilog hex via `objcopy -Overilog`. UART3 is used for stdio (`-DSTDIO_UART=UART_3 -DSTDIO_PINMUX=PINMUX_3`).

### Running on hardware

```bash
# Flash chango_fabric.hex to E1x
# Then on host:
sudo stty -F /dev/ttyUSB0 115200 raw -echo
cat /dev/ttyUSB0 | play -t raw -r 8000 -e unsigned -b 8 -c 1 -
```

### PnR visualization

```bash
make pnrviz       # fabric build + SVGs of placed-and-routed kernels
make pnrviz_pdf   # convert SVGs to PDFs via rsvg-convert
```

## E1x conventions and gotchas

- `__efficient__` attribute on functions that should compile to E1x fabric
- `stop_propagation_*()` to prevent constant folding of test inputs
- `restrict` pointers for alias analysis
- Q15 fixed-point: multiply → int32_t accumulate → `FIXED_ROUND(x)` = `(x + (1 << 14)) >> 15`
- **NEVER use division inside `__efficient__` functions** — it hangs the fabric. Use precomputed multiply-shift approximations instead. Example: `x / 25 ≈ (x * 1311) >> 15`.
- **Watch for int32 overflow** in multiply-shift. With 25 tones, max mix ~816K, `816K * 1311 = 1.07B` fits int32. With 100 tones it overflows — would need two-step: `(x >> 2) * 1311 >> 15`.
- **Region pixel counts must be power of 2** so averaging can use shift instead of division. 40x40 image / 5x5 grid = 8x8 regions = 64 pixels → shift by 6.
- `__effcc_parallel(N)` and `__effcc_ignore_memory_order { ... }` pragmas are available with `-DUSE_PRAGMAS` but were found to be unreliable. Manual unrolling by 4 is safer.
- The sim backend (`--sim`) is very slow with unrolled code — it can appear to hang. Test unrolled kernels on fabric only.
- `-DUSE_PRAGMAS` enables the pragma macros; without it they become no-ops via `test_common.h`.
- `--disable-memory-ordering` compiler flag exists but was found to be unreliable. Avoid it.

## UART and audio output details

- E1x UART clock is 7813120 Hz. At 115200 baud, the actual rate is ~108516 Hz (5.8% error). This is the only baud rate that works reliably; 230400, 460800, and 576000 all failed due to clock divisor limitations.
- Audio output uses `eff_uart_putc(STDIO_UART, byte)` directly — NOT printf. This sends raw binary bytes.
- `--gc-sections` is NOT used in the fabric link. It strips the UART init constructor (`__eff_config_stdio`) when printf isn't called in the hot loop, leaving the UART uninitialized.
- `--allow-multiple-definition` is needed because `libeff.a` and `libMonacoTarget.a` (from the compiler) both define IRQ handler symbols.
- The host serial port MUST be set to raw mode with `sudo stty -F /dev/ttyUSB0 115200 raw -echo`. Without `sudo` on WSL, stty fails silently. Without `raw`, the terminal driver interprets binary bytes as control characters (0x04=EOF closes the connection, 0x03=SIGINT kills cat).
- Mixing text and binary on the same UART stream is fragile. An odd-length text prefix misaligns all subsequent 16-bit samples. The current 8-bit format avoids this problem entirely since every byte is a sample.

## IMU → LPF mapping

The IMU SWIL generates a triangle wave on `accel_x` (one cycle over the full render) and `accel_y` (half rate). In main.c these map to:
- `accel_x` (-1000..+1000 milli-g) → `lpf_cutoff_idx` (0..15)
- `accel_y` (-1000..+1000 milli-g) → `lpf_q_idx` (0..7)

Coefficients are looked up from the precomputed table each frame. This produces an audible filter sweep in the output.

## Image scaling

`scale_image()` performs nearest-neighbor resize from camera resolution to 256×256 using multiply-shift (no division):
- `input_y = oy * src_h >> 8`
- `input_x = ox * src_w >> 8`

Source dimensions (`SRC_WIDTH`, `SRC_HEIGHT`) are defined in gen_data.py and emitted to the header. The scaled image is always 256×256 (`IMG_WIDTH × IMG_HEIGHT`).

To support a different camera resolution, just change `SRC_WIDTH`/`SRC_HEIGHT` in gen_data.py. The scaler handles any source size.

## Precomputed constants in main.c

These avoid division on the fabric and must be updated if the grid or image size changes:

```
REGION_W = IMG_WIDTH / NUM_GRID_X     (currently 256/4 = 64)
REGION_H = IMG_HEIGHT / NUM_GRID_Y    (currently 256/4 = 64)
REGION_SHIFT = log2(REGION_W * REGION_H) = log2(4096) = 12
INV_NUM_TONES = 1    (16 is power of 2, pure shift)
INV_SHIFT = 4        (log2(16) = 4)
```

Power-of-2 grid options on 256×256:
- 4×4 = 16 tones, 64×64 regions, region_shift=12, inv_shift=4
- 8×8 = 64 tones, 32×32 regions, region_shift=10, inv_shift=6

## Future work

1. **Real camera driver** — hardware ordered, interface ready (`camera_t`). Write `camera_hw.c`.
2. **Real audio output driver** — interface ready (`audio_out_t`). I2S DAC would bypass UART bandwidth limits entirely. Write `audio_out_hw.c`.
3. **Real IMU driver** — interface ready (`imu_t`). Write `imu_hw.c`.
4. **Higher baud rate** — needs SDK UART clock patch. 576000 baud would allow 16-bit audio.
5. **More synthesis parameters** — IMU/image could drive tone selection, panning, waveform shape.
6. **Optimization** — manual unrolling by 4 in pixelate inner loop is done. Synthesize unrolling was attempted but caused hangs on both sim and fabric. The synthesize inner loop (25 tones × 512 samples = 12,800 iterations per frame) is the main bottleneck.

## Pitfalls learned

- **No division on fabric.** This was the #1 cause of hangs on hardware. Even `width / grid_x` with runtime values hangs. All division must be precomputed in main.c and passed as parameters.
- **int32 overflow in multiply-shift.** `mix * 5243` for divide-by-100 overflowed int32 (max 2.1B), producing noise that sounded like static. With 25 tones the single-step `mix * 1311` fits.
- **--gc-sections strips UART init.** Without printf in the hot path, the linker removes the UART constructor. Don't use --gc-sections for the fabric build.
- **stty requires sudo on WSL.** Without sudo, `stty -F /dev/ttyUSB0 ...` fails silently, leaving the port in cooked mode where binary bytes like 0x04 (Ctrl-D) close the connection.
- **Odd-length text prefix misaligns 16-bit audio.** "CHANGO\n" is 7 bytes; all subsequent int16 sample pairs are shifted by 1 byte = static. Fixed by switching to 8-bit samples (1 byte each, no alignment issue).
- **PAT tokens** for `gh` need `repo` and `read:org` scopes. Push uses `x-access-token:TOKEN@github.com` in the remote URL (clean it up after push).
- **git add -A is dangerous.** Only add specific files by name to keep commits clean.
