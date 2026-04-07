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
camera.capture() → pixelate() → synthesize() → lowpass() → audio_out.write()
                                      ↑                ↑
                                 sine table       IMU controls
                                 phase incs       cutoff/resonance
```

Each frame: capture an image, divide into 10x10 grid, compute average intensity per region, generate 256 audio samples (one tone per region, amplitude = intensity), apply biquad low-pass filter, push to output.

### Key design constraints

- **No floating point.** E1x has no FPU. All arithmetic is integer. Sine waves use a 256-entry Q15 lookup table. Filter coefficients are Q15 (int32_t scaled by 32767). Phase accumulators are uint32_t.
- **No filesystem.** E1x is embedded. Audio output goes over printf/UART. The host-side `uart_to_raw.py` script decodes it back to raw PCM.
- **SWIL pattern.** All hardware interfaces (camera, audio output, IMU) use a function-pointer struct pattern (`camera_t`, `audio_out_t`, `imu_t`). Each has a software-in-the-loop (SWIL) implementation for testing without hardware. When hardware arrives, write a new `_hw.c` implementing the same interface and swap the `_create()` call in main.c.

### Files

| File | Role |
|------|------|
| `chango.c` | `__efficient__` kernels: `pixelate()`, `synthesize()`, `lowpass()` |
| `main.c` | Frame loop wiring all the pieces together |
| `camera.h` / `camera_swil.c` | Camera interface + SWIL (returns embedded test image) |
| `audio_out.h` / `audio_out_swil.c` | Audio output interface + SWIL (printf samples over UART) |
| `imu.h` / `imu_swil.c` | IMU interface + SWIL (triangle-wave accel sweep) |
| `uart_to_raw.py` | Host-side: decodes UART text → raw 16-bit PCM binary |
| `gen_data.py` | Generates `chango_data.h.inc`: test image, sine table, phase increments, LPF coefficient table |
| `CMakeLists.txt` | For integration into the Efficient apps build tree (`add_eff_app`) |
| `Makefile` | Standalone build with `effcc` |

### Generated data (`chango_data.h.inc`, via `gen_data.py`)

- 100x100 grayscale test image (synthetic gradient + circle pattern)
- 256-entry Q15 sine lookup table
- 100 phase increments (one per tone, for 10x10 grid)
- Biquad LPF coefficient table: 16 cutoff frequencies (200-3800 Hz, log-spaced) × 8 Q values (0.5-8.0, log-spaced) × 5 coefficients (b0, b1, b2, a1, a2) in Q15

### Audio format

- 16-bit signed PCM, mono, 8000 Hz sample rate
- 256 samples per frame (~30 fps equivalent)
- Play with: `play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw`

## Building

Requires `effcc` from the Efficient Computer toolchain. Activate the venv first:

```bash
source ~/venv/bin/activate
make sim                    # build simulation binary
./chango_sim | python3 uart_to_raw.py > chango_output.raw  # run + decode
play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw  # listen
# or just:
make play
```

The E1x SDK headers are at `/home/blucia/cvsandbox/apps/eff_sdk/include/` and stop-propagation helpers at `/home/blucia/cvsandbox/apps/include/`. Example E1x apps (biquad_filter, conv3x3, etc.) are at `/home/blucia/cvsandbox/apps/micro/`.

### E1x conventions used

- `__efficient__` attribute on functions that should compile to E1x fabric
- `enterProfileRegion()` / `exitProfileRegion()` for hardware profiling
- `stop_propagation_*()` to prevent constant folding of test inputs
- `restrict` pointers for alias analysis
- Q15 fixed-point: multiply → int32_t accumulate → `FIXED_ROUND(x)` = `(x + (1 << 14)) >> 15`

## IMU → LPF mapping

The IMU SWIL generates a triangle wave on `accel_x` (one cycle over the full render) and `accel_y` (half rate). In main.c these map to:
- `accel_x` (-1000..+1000 milli-g) → `lpf_cutoff_idx` (0..15)
- `accel_y` (-1000..+1000 milli-g) → `lpf_q_idx` (0..7)

Coefficients are looked up from the precomputed table each frame. This produces an audible filter sweep in the SWIL output.

## Future work (from README.md)

1. **Real camera driver** — hardware ordered, interface ready (`camera_t`). Write `camera_hw.c`.
2. **Real audio output driver** — interface ready (`audio_out_t`). Likely I2S DAC. Write `audio_out_hw.c`.
3. **Real IMU driver** — interface ready (`imu_t`). Write `imu_hw.c`.
4. **More synthesis parameters** — could modulate tone selection, panning, waveform shape, etc. from IMU or image features.
5. **Higher sample rate** — currently 8000 Hz for simplicity; 16000 or 44100 would improve fidelity.

## Pitfalls learned

- **Don't write binary to stdout.** On E1x there's no filesystem; all output goes over printf/UART. The SWIL audio output prints decimal sample values framed with `CHANGO_AUDIO_START`/`CHANGO_AUDIO_END` markers. The host script `uart_to_raw.py` decodes these.
- **PAT tokens** for `gh` need `repo` and `read:org` scopes. The `gh auth setup-git` credential helper doesn't work in WSL without a browser, so push uses `x-access-token:TOKEN@github.com` in the remote URL (clean it up after push).
