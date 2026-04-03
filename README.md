# effchango

Chango musical instrument targeting the Efficient E1x processor.

Based on the original [PhotoChango](https://github.com/blucia0a/PhotoChango) (2011-2012), which converts camera images to audio by dividing the image into a grid of square regions, associating a tone with each region, and using the average pixel intensity of the region as the amplitude of the tone. All region tones are mixed into a single audio stream.

This version is written in pure integer arithmetic (no floating point) to target the E1x embedded platform. Sine wave generation uses a 256-entry Q15 lookup table with fixed-point phase accumulators.

## Pipeline

1. **Image capture** — abstracted behind `camera.h` interface; currently uses a software-in-the-loop (SWIL) implementation that returns a synthetic test image
2. **Pixelation** — divides image into a 10x10 grid, computes average intensity per region
3. **Synthesis** — generates a sine tone per region, amplitude proportional to intensity
4. **Mixing** — sums and averages all 100 tones into 16-bit signed PCM
5. **Audio output** — abstracted behind `audio_out.h` interface; SWIL implementation prints samples over printf/UART, decoded on the host by `uart_to_raw.py`

## Building

Requires `effcc` (Efficient Computer compiler) on your PATH.

```bash
# Generate input data and build for simulation
make sim

# Run and play audio
./chango_sim | python3 uart_to_raw.py > chango_output.raw
play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw

# Or in one step:
make play
```

## Files

| File | Description |
|------|-------------|
| `chango.c` | `__efficient__` kernels: `pixelate()` and `synthesize()` |
| `main.c` | Main frame loop — capture, process, synthesize, output |
| `camera.h` | Camera capture interface (function pointers) |
| `camera_swil.c` | SWIL camera: returns embedded test image |
| `audio_out.h` | Audio output interface (function pointers) |
| `audio_out_swil.c` | SWIL audio output: prints samples over printf/UART |
| `uart_to_raw.py` | Host-side decoder: UART text to raw PCM binary |
| `gen_data.py` | Generates test image, Q15 sine table, and tone phase increments |
| `CMakeLists.txt` | For integration into the Efficient apps build tree |
| `Makefile` | Standalone build (sim/fabric targets) |

## Future work

- **Low-pass filter in synthesis pipeline.** Add a parameterized low-pass filter (biquad or similar) with cutoff frequency and resonance inputs, implemented in integer arithmetic. This would allow shaping the timbre of the mixed tones beyond raw sine waves.
- **Sensor-driven parameter control.** Allow image processing results or other sensor inputs (e.g., IMU) to modulate synthesis parameters like filter cutoff, resonance, tone selection, or panning in real time.
- **IMU interface.** Add an IMU (accelerometer/gyroscope) input interface following the same SWIL/abstraction model as the camera and audio output (`imu.h` + `imu_swil.c`), anticipating the addition of hardware in the future. The IMU would generate control signals for goal (2) above — for example, tilting the device could sweep the filter cutoff or shift the tone map.
