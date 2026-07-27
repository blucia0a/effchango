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
camera.capture(160x120) → scale_image(256x256) → pixelate(4x4) → synthesize(16 tones) → lowpass() → audio_out.write()
                                                                        ↑                     ↑
                                                                   sine table            IMU controls
                                                                   phase incs            cutoff/resonance
```

Each frame: capture a QQVGA image (160×120), nearest-neighbor scale to 256×256, divide into 4×4 grid (64×64 pixel regions), compute average intensity per region, generate `SAMPLES_PER_FRAME` audio samples (one tone per region, amplitude = intensity), apply biquad low-pass filter, send to UART as 8-bit unsigned PCM.

**Camera resolution is QQVGA 160×120.** That is what every hardware-validated demo in `sales_demo_kit` runs, and chango averages the frame into a 4×4 grid regardless, so QVGA/VGA only cost SPI readout time. Switch with the `HIMAX_*` defines at the top of `main_hm0360.c`; `scale_image()` handles any source size.

### Key design constraints

- **No floating point.** E1x has no FPU. All arithmetic is integer. Sine waves use a 256-entry Q15 lookup table. Filter coefficients are Q15 (int32_t scaled by 32767). Phase accumulators are uint32_t.
- **No division on the fabric.** Division hangs or runs extremely slowly on the E1x fabric. All division is replaced with precomputed multiply-shift approximations or bit shifts. Region averaging uses `>> region_shift` (power-of-2 region sizes). Tone averaging uses `(mix * inv_num_tones) >> inv_shift`.
- **No filesystem.** E1x is embedded. Audio output goes over UART as raw binary bytes.
- **SWIL pattern.** All hardware interfaces (camera, audio output, IMU) use a function-pointer struct pattern (`camera_t`, `audio_out_t`, `imu_t`). Each has a software-in-the-loop (SWIL) implementation for testing without hardware. When hardware arrives, write a new `_hw.c` implementing the same interface and swap the `_create()` call in main.c.

### Files

| File | Role |
|------|------|
| `chango.c` | `__efficient__` kernels: `scale_image()`, `pixelate()`, `synthesize()`, `lowpass()` |
| `main_hm0360.c` | **Hardware** main: minimal-camera bring-up verbatim + chango spliced into its capture loop. Used when `HW_CAMERA=1` |
| `main.c` | **SWIL** main: frame loop over the `camera_t` abstraction |
| `camera.h` / `camera_swil.c` | Camera interface + SWIL (returns embedded test image) |
| `hm0360.h` / `hm0360.c` | HM0360 register defs, config tables, and `hm0360_{read,write}_reg` (copied from `sales_demo_kit` branch `kit-dash-polish`, path `demos/minimal_camera/`) |
| `audio_out.h` / `audio_out_swil.c` | Audio output interface + SWIL (raw 8-bit PCM bytes over UART) |
| `imu.h` / `imu_swil.c` | IMU interface + SWIL (triangle-wave accel sweep) |
| `listen_audio.py` | **Host audio listener** — `--measure` / `--wav` / `--raw` / `--play` on the 8-bit PCM stream |
| `grab_frames.py` | Host frame grabber for `CAMERA_DEBUG`, headless: prints per-frame stats, writes PNGs |
| `view.py` | Host GUI viewer for `CAMERA_DEBUG` — DEADBEEF SOF, OpenCV window |
| `uart_to_raw.py` | Host-side: decodes text-mode UART output (legacy, not used with binary mode) |
| `gen_data.py` | Generates `chango_data.h.inc`: test image, sine table, phase increments, LPF coefficient table |
| `requirements.txt` | Host Python deps (`pyserial`, `numpy`, `opencv-python`); install into `./venv` |
| `CMakeLists.txt` | For integration into the Efficient apps build tree (`add_eff_app`) |
| `Makefile` | Standalone build with `effcc` |

### Why the hardware path bypasses `camera_t`

There is deliberately **no `camera_t` implementation for the HM0360**. An adapter
that reshaped the reference bring-up to fit `init`/`capture`/`shutdown` produced
a board that emitted nothing at all — it hung before the clock boost — while the
unmodified reference streamed frames fine on the same hardware and toolchain.
Rather than keep bisecting a working reference, `main_hm0360.c` keeps the demo's
call order, register tables, pin map, clock boost, UART divisors and VSYNC
handshake **byte-for-byte** and only changes what happens to a captured frame.
Do not "tidy" that sequence; the ordering is the part that works.

### Generated data (`chango_data.h.inc`, via `gen_data.py`)

- 320×240 QVGA grayscale test image (synthetic gradient + circle pattern)
- 256-entry Q15 sine lookup table
- 16 phase increments (one per tone, for 4×4 grid)
- Biquad LPF coefficient table: 16 cutoff frequencies (200-3800 Hz, log-spaced) × 8 Q values (0.5-8.0, log-spaced) × 5 coefficients (b0, b1, b2, a1, a2) in Q15

### Audio format

- **8-bit unsigned PCM**, mono, nominally 8000 Hz
- Converted from internal 16-bit signed by taking high byte + 128: `(sample >> 8) + 128`
- No framing, no header, no escaping — every byte on the wire is a sample, so the
  stream can be joined at any point
- 1920 samples per frame by default (see "Sample rate")
- On a HW-camera build the wire runs at 929370 baud = 92,937 bytes/sec, ~11x the
  8 kHz sample rate, so the UART is nowhere near the limiter
- Host side: `listen_audio.py` (see "Running on hardware")

## Building

### Toolchain locations

Neither `effcc` nor the SDK is on `PATH`; the Makefile points at build trees directly:

| Make var | Path | Notes |
|---|---|---|
| `EFFCC_BIN` | `/home/blucia/effcc_bld/bin` | `effcc`, `fabric-translate`, `ld.lld`, … |
| `SDK_BUILD` | `/home/blucia/eff_sdk_bld` | prebuilt `libeff.a` + `drivers/*/scalar/*.a` |
| `SDK_ROOT` | `/home/blucia/cvsandbox/apps/eff_sdk` | headers (`/home/blucia/eff_sdk` symlinks here) |
| `OBJCOPY` | `objcopy` | **must be GNU binutils** — see below |

Two gotchas:

- **`OBJCOPY` must be GNU `objcopy`, not the toolchain's `llvm-objcopy`.** Only
  binutils has the `verilog` writer that `objcopy -Overilog` needs to produce
  the `.hex` `eff-flash` consumes; `llvm-objcopy` fails with
  `invalid output format: 'verilog'`.
- **The prebuilt SDK needs no `EFF_STDIO_PORT=3`.** For `EFF_ARCH_E1X` the
  SDK's own defaults are already `STDIO_UART=UART_3` / `STDIO_PINMUX=PINMUX_3`
  (`include/eff/drivers/uart.h`), which is exactly what `STDIO_OPTS` selects.
  `make sdk` only rebuilds the tree in place; it does not reconfigure it.

### Sim build (functional testing on host)

```bash
make sim
```

Note: sim builds with `--sim` flag. The sim backend is very slow with manually unrolled `__efficient__` functions — keep kernels simple for sim testing. The sim does NOT link the SDK (no UART driver); audio goes through putchar/stdout.

### Fabric build (for E1x hardware)

```bash
make fabric                                        # SWIL camera, audio output (default)
make fabric HW_CAMERA=1                            # HM0360 camera -> chango -> audio
make fabric HW_CAMERA=1 CAMERA_DEBUG=1             # HM0360 + stream raw frames over UART
make fabric HW_CAMERA=1 SAMPLES_PER_FRAME=2048     # retune the achieved sample rate
```

Compiles with `-O3 -flto --target=e1x -DUSE_PRAGMAS`, links against SDK `libeff.a` with `--whole-archive`, converts to Verilog hex via `objcopy -Overilog`. UART3 is used for stdio (`-DSTDIO_UART=UART_3 -DSTDIO_PINMUX=PINMUX_3`).

Build toggles:
- `HW_CAMERA=1` builds `main_hm0360.c` + `hm0360.c` instead of `main.c` + `camera_swil.c`, and defines `USE_HW_CAMERA`.
- `CAMERA_DEBUG=1` emits `0xDE 0xAD 0xBE 0xEF` + `width*height` raw pixel bytes per frame over `STDIO_UART` and skips synth/audio. Use with `grab_frames.py` (headless) or `view.py` (GUI) to confirm the camera is producing real frames.
- `SAMPLES_PER_FRAME=N` overrides the default 1344. See "Sample rate" below.
- `IMU=1` re-enables the synthetic IMU tilt sweep of the LPF (off by default). See "IMU → LPF mapping".
- `CAM_STREAMING=0` reverts to the donor's snapshot capture path. Default is 1 (continuous streaming) — see Pitfalls.
- `INTENSITY_DEBUG=1` emits the 4x4 intensity grid instead of audio, for `diag_intensity.py`.
- `CAM_TEST_PATTERN=0x01` forces the sensor's internal pattern generator (0x01 colour bar, 0x21 walking 1's) — proves the I2C+SPI readout path is live independently of exposure.

### Running on hardware

Two baud regimes:

- **SWIL builds (default `make fabric`):** UART runs at ~108 000 baud
  (SDK's `__eff_config_stdio` sets this for E1X).
- **HW_CAMERA builds:** `main_hm0360.c` writes `0x0201` to the PMU
  clock control register (`0x400310`) to boost the system clock for the
  camera's PCLKO/SPI timing. The same write changes the UART base clock,
  so it then reconfigures `STDIO_UART` to ~929 370 baud
  (oscr=0xE, baud=558080/3). Without this the camera never produces
  VSYNC after a snapshot trigger.

`eff-flash` lives at `/home/blucia/cvsandbox/E1-Dev-Kit/pc-host/eff-flash`.
The console is `/dev/eff-console` (symlink to `/dev/ttyACM2` here), **not**
`/dev/ttyUSB0`.

```bash
EFF_FLASH=/home/blucia/cvsandbox/E1-Dev-Kit/pc-host/eff-flash
source venv/bin/activate

# --- HW camera -> audio (the main event) ---
make fabric HW_CAMERA=1
$EFF_FLASH -f chango_fabric.hex -b 929370
python listen_audio.py /dev/eff-console --measure            # check the rate first
python listen_audio.py /dev/eff-console --wav out.wav --seconds 20
python listen_audio.py /dev/eff-console --play               # live, via SoX

# --- Camera check: raw frame stream ---
make fabric HW_CAMERA=1 CAMERA_DEBUG=1
$EFF_FLASH -f chango_fabric.hex -b 929370
python grab_frames.py /dev/eff-console 160 120 --baud 929370 -n 5   # headless + PNGs
python view.py /dev/eff-console 160 120 --baud 929370               # GUI

# --- Default (SWIL) fabric, no camera: stays at 108000 ---
make fabric
$EFF_FLASH -f chango_fabric.hex -b 108000
python listen_audio.py /dev/eff-console --baud 108000 --wav out.wav
```

Always read with pyserial, not `cat` — the kernel tty driver drops bytes at
these non-standard baud rates. `eff-flash -b` sets the RP2040-bridge↔E1x baud
and **must match the device-side rate**; it persists until the programmer is
reset.

### Sample rate

The firmware has no timer pacing it — the camera's PMU clock boost rules out the
mtimer (see Pitfalls) — so it emits samples as fast as the capture+synthesis loop
produces them, and the rate is *not* locked to 8000 Hz.

Measured on this EVK at QQVGA, the loop has a large fixed per-frame cost (the
wait for the next frame boundary, plus `scale_image` and `pixelate` over 65536
pixels each) and ~15 µs per generated sample. So `SAMPLES_PER_FRAME` is the
sample-rate knob — it amortizes that fixed cost:

| capture mode | `SAMPLES_PER_FRAME` | achieved rate | image updates/s |
|---|---|---|---|
| streaming | **1344 (default)** | **~7950 Hz** | ~5.9 |
| streaming | 1920 | ~11450 Hz (+6.2 semitones) | ~6.0 |
| snapshot | 1920 | ~7950 Hz | ~4.1 |
| snapshot | 512 | ~2600 Hz | ~5.1 |

**The default is specific to `CAM_STREAMING=1`.** Snapshot's trigger→VSYNC
latency alone is tens of ms, so it needs a larger value to reach the same rate
and still ends up less responsive.

The rate jitters ~±1% run to run (the VSYNC wait quantizes to sensor frame
boundaries), which is under a quarter semitone — inaudible. Play at 8000 Hz for
the pitch `TONE_FREQS` was designed around.

Retune with `make fabric HW_CAMERA=1 SAMPLES_PER_FRAME=N` plus
`listen_audio.py --measure` after any change to the resolution or the kernels.

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

- Without the camera's clock boost, the SDK's `__eff_config_stdio` sets E1x stdio to 108000 baud. With the boost plus `oscr=0xE, baud=558080/3` it runs at ~929370 (nominal 921600) — the rate the reference camera demo uses.
- Audio output uses `eff_uart_putc(STDIO_UART, byte)` directly — NOT printf. This sends raw binary bytes.
- `--gc-sections` is NOT used in the fabric link. It strips the UART init constructor (`__eff_config_stdio`) when printf isn't called in the hot loop, leaving the UART uninitialized.
- `--allow-multiple-definition` is needed because `libeff.a` and `libMonacoTarget.a` (from the compiler) both define IRQ handler symbols.
- **Read with pyserial, not `cat`.** The kernel tty driver drops bytes at these non-standard baud rates, and in cooked mode it interprets binary as control characters (0x04=EOF closes the connection, 0x03=SIGINT kills `cat`). `listen_audio.py` and `grab_frames.py` both open the port directly, so no `stty` is needed.
- Mixing text and binary on the same UART stream is fragile. An odd-length text prefix misaligns all subsequent 16-bit samples. The current 8-bit format avoids this problem entirely since every byte is a sample.

## Biquad int32 overflow

`lowpass()` accumulates into an `int32_t`:

```c
int32_t acc = x * b0 + x1 * b1 + x2 * b2 - y1 * a1 - y2 * a2;
```

With Q15 coefficients and `|x|, |y| <= 32767`, the worst-case magnitude is
`32767 * sum(|coef|)`. **Only 19 of the 128 (cutoff, Q) entries in `lpf_coeffs`
stay inside int32.** Biquad coefficients legitimately exceed 1.0 — `a1` reaches
-64100 in Q15 — and a single product like `32767 * 64500` is already 2.1e9, at
the int32 limit before anything is summed. High cutoffs and high Q are the worst:
entry (15, 7) needs 7.4e9, about 3.4x over.

Overflowing produces loud garbage that barely tracks the image, which reads as
"the instrument does not respond". Check a candidate entry before using it:

```bash
./venv/bin/python -c "
import re
v=[int(x) for x in re.findall(r'-?\d+', re.search(r'lpf_coeffs\[640\] = \{(.*?)\};',
  open('chango_data.h.inc').read(), 16).group(1))]
ci,qi=12,0; o=(ci*8+qi)*5
print(32767*sum(abs(c) for c in v[o:o+5]), 'vs', 2**31)"
```

`LPF_CUTOFF_IDX 12` (2109 Hz) with `LPF_Q_IDX 0` (Q=0.50) is the highest-cutoff
safe entry, at 1.70x headroom, and is the default. It passes the whole tone table
(top tone 1568 Hz).

This also affects `IMU=1`: the sweep walks cutoff 0..15 and Q 0..7, so it spends
much of its time in overflowing entries. Some of that build's "character" is
distortion. A real fix — accumulating in `int64_t`, or pre-scaling the
coefficients and shifting — would make the whole table usable, but it touches a
kernel that is otherwise tuned, so it is left as a decision rather than done.

## IMU → LPF mapping

**Off by default on hardware** (`USE_IMU 0` in `main_hm0360.c`). There is no IMU
part wired up on this hat — `imu_swil.c` fabricates the tilt on-device as a
triangle wave, so the sweep played whether or not anything was moving. That is a
synthetic effect rather than a response to the world, so the hardware build now
runs the biquad at a fixed setting and lets the image be the only thing shaping
the sound.

- `make fabric HW_CAMERA=1` → fixed filter at `LPF_CUTOFF_IDX` / `LPF_Q_IDX`,
  defaulting to the top cutoff (3800 Hz, passes all of `TONE_FREQS`) and the
  lowest Q (0.5, no resonant peak to clip against). Both are `-D`-overridable.
- `make fabric HW_CAMERA=1 IMU=1` → restores the sweep.

With the sweep on, the SWIL IMU generates a triangle wave on `accel_x` (one cycle
per `IMU_SWEEP_FRAMES`) and `accel_y` (half rate), mapped to:
- `accel_x` (-1000..+1000 milli-g) → `lpf_cutoff_idx` (0..15)
- `accel_y` (-1000..+1000 milli-g) → `lpf_q_idx` (0..7)

Coefficients are looked up from the precomputed table each frame. Measured effect:
spectral centroid spread over 15 s was 1.53× with the sweep on, 1.26× with it off
(the remainder being genuine scene variation).

`main.c` (the SWIL build) still always uses the sweep; the toggle is only on the
hardware path.

A real IMU would need a driver written from scratch — nothing in the SDK or
`sales_demo_kit` covers one — but `E1-Dev-Kit/Hat_bringup_drivers/sensor_drivers/bmi323.{c,h}`
is a Bosch BMI323 driver that would be the starting point.

## Image scaling

`scale_image()` performs nearest-neighbor resize from camera resolution to 256×256 using multiply-shift (no division):
- `input_y = oy * src_h >> 8`
- `input_x = ox * src_w >> 8`

Source dimensions (`SRC_WIDTH`, `SRC_HEIGHT`) are defined in gen_data.py and emitted to the header. The scaled image is always 256×256 (`IMG_WIDTH × IMG_HEIGHT`).

To support a different camera resolution, just change `SRC_WIDTH`/`SRC_HEIGHT` in gen_data.py. The scaler handles any source size.

## Precomputed constants in main.c / main_hm0360.c

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

1. ~~**Real camera driver**~~ — done: `main_hm0360.c`, QQVGA, verified on hardware.
2. **Cut the fixed per-frame cost (~190 ms).** This is now the main limiter: it
   forces `SAMPLES_PER_FRAME=1920` to reach 8 kHz, which caps image
   responsiveness at ~4 fps. Most of it is `scale_image` + `pixelate` doing
   131072 pixel operations to produce 16 region averages from a 19200-pixel
   source. Scaling to 64×64 instead of 256×256 (region 16×16, `REGION_SHIFT` 8)
   would cut that ~16×; it needs the hardcoded `256`/`>> 8` in `scale_image()`
   parameterized. The rest is the snapshot trigger→VSYNC wait (~40 ms), which
   streaming mode would remove (see `sales_demo_kit` RESULTS-CAM-020).
3. **Auto-exposure.** The build runs the donor's static-exposure baseline, so a
   dark or blown-out room flattens the intensity grid and the sound with it.
   `sales_demo_kit` has both a sensor-HW-AE path (`HM0360_HW_AE` +
   `HM0360_LINEAR_TONEMAP`, RESULTS-CAM-022/024) and a firmware AE loop
   (RESULTS-CAM-025) — though neither was on-hat validated there.
4. **Real audio output driver** — interface ready (`audio_out_t`). An I2S DAC would bypass UART bandwidth limits entirely. Write `audio_out_hw.c`.
5. **Real IMU driver** — interface ready (`imu_t`); the synthetic sweep is now off by default. Nothing in the SDK or `sales_demo_kit` has an IMU driver, but `E1-Dev-Kit/Hat_bringup_drivers/sensor_drivers/bmi323.{c,h}` (Bosch BMI323) is a starting point. Write `imu_hw.c`.
6. **More synthesis parameters** — IMU/image could drive tone selection, panning, waveform shape.
7. **Clipping.** With a resonant LPF (Q up to 8) the mix hits the rails on bright
   scenes. Characterful, but a makeup-gain or soft-clip stage would tame it.

## Pitfalls learned

- **`MODE_SELECT` is a state, not a pulse — the donor demo's snapshot loop never
  re-captures.** `MODE_SELECT` (0x0100) `[2:0]` selects a state: 000 sleep, 001
  continuous streaming, 011 snapshot-with-N-frames. `demos/minimal_camera` sits
  in snapshot mode and rewrites `0x03` every iteration, but only the first write
  is a `000 -> 011` transition; every later one is `011 -> 011`, which the state
  machine ignores. **The sensor never re-exposes and the readout returns the same
  frame forever.** Symptom: a perfectly plausible-looking live video feed that is
  actually one frozen frame, so covering the lens changes nothing. Diagnose it by
  watching `mean`/`std` across frames — frozen content holds them constant to
  ~0.1 even while per-pixel diffs look large (the readout byte offset jitters, so
  frames look different without being different).
  Fix: **continuous streaming** (`CAM_STREAMING=1`, the default) — the sensor
  free-runs, so syncing to a VSYNC edge always lands on fresh pixels. For the
  snapshot path, write sleep before each trigger so it is a real transition.
- **`lowpass()` overflows int32 for most of the coefficient table.** See "Biquad
  int32 overflow" below. Only 19 of 128 `lpf_coeffs` entries are safe.
- **Never `sleep_ms()` after the PMU clock boost.** The boost
  (`*0x400310 = 0x0201`) re-bases the clock the timer runs off without the SDK
  knowing, so anything timer-driven past that point can hang the board with no
  output at all. Every camera demo in `sales_demo_kit` keeps the same
  "timer-free discipline" past the boost, and the reference only sleeps *before*
  it. Do any settling wait before the boost.
- **Don't reshape the camera bring-up.** See "Why the hardware path bypasses
  `camera_t`" above: a refactored-but-equivalent-looking init produced a board
  that emitted nothing, while the verbatim reference worked on the same hardware.
- **When hardware goes silent, build the reference as a control.** Compiling
  `sales_demo_kit`'s `demos/minimal_camera` unchanged against this same
  toolchain and SDK proved in one flash cycle that the hardware, toolchain, baud
  and hat were all fine — which is what localized the fault to our code. Much
  faster than bisecting our own firmware blind.
- **No division on fabric.** This was the #1 cause of hangs on hardware. Even `width / grid_x` with runtime values hangs. All division must be precomputed in main.c and passed as parameters.
- **int32 overflow in multiply-shift.** `mix * 5243` for divide-by-100 overflowed int32 (max 2.1B), producing noise that sounded like static. With 25 tones the single-step `mix * 1311` fits.
- **--gc-sections strips UART init.** Without printf in the hot path, the linker removes the UART constructor. Don't use --gc-sections for the fabric build.
- **Avoid `stty` + `cat` entirely; use pyserial.** `stty` needs `sudo` on WSL and fails silently without it, leaving the port cooked so binary bytes like 0x04 (Ctrl-D) close the connection. The host scripts open the port directly instead.
- **Odd-length text prefix misaligns 16-bit audio.** "CHANGO\n" is 7 bytes; all subsequent int16 sample pairs are shifted by 1 byte = static. Fixed by switching to 8-bit samples (1 byte each, no alignment issue).
- **PAT tokens** for `gh` need `repo` and `read:org` scopes. Push uses `x-access-token:TOKEN@github.com` in the remote URL (clean it up after push).
- **git add -A is dangerous.** Only add specific files by name to keep commits clean.
