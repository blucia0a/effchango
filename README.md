# effchango

Chango musical instrument targeting the Efficient E1x processor.

Based on the original [PhotoChango](https://github.com/blucia0a/PhotoChango) (2011-2012), which converts camera images to audio by dividing the image into a grid of square regions, associating a tone with each region, and using the average pixel intensity of the region as the amplitude of the tone. All region tones are mixed into a single audio stream.

This version is written in pure integer arithmetic (no floating point) to target the E1x embedded platform. Sine wave generation uses a 256-entry Q15 lookup table with fixed-point phase accumulators.

## Pipeline

1. **Image capture** — currently uses a synthetic test image embedded as a C header (camera interface TBD)
2. **Pixelation** — divides image into a 10x10 grid, computes average intensity per region
3. **Synthesis** — generates a sine tone per region, amplitude proportional to intensity
4. **Mixing** — sums and averages all 100 tones into 16-bit signed PCM
5. **Audio output** — currently writes to a raw PCM file (audio driver interface TBD)

## Building

Requires `effcc` (Efficient Computer compiler) on your PATH.

```bash
# Generate input data and build for simulation
make sim

# Run and play audio
./chango_sim
play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw
```

## Files

| File | Description |
|------|-------------|
| `chango.c` | `__efficient__` kernels: `pixelate()` and `synthesize()` |
| `main.c` | Test harness — runs the pipeline, writes raw PCM output |
| `gen_data.py` | Generates test image, Q15 sine table, and tone phase increments |
| `CMakeLists.txt` | For integration into the Efficient apps build tree |
| `Makefile` | Standalone build (sim/fabric targets) |
