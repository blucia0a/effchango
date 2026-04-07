#!/usr/bin/env python3
"""Generate input data for effchango: test image, sine table, and tone phase increments."""

import argparse
import math
import struct
import os

# Musical note frequencies (Hz) - pentatonic-ish spread across octaves
# Matches the spirit of the original PhotoChango tones
TONE_FREQS = [
    # Row 0 (bottom) - low octave
    130.81, 146.83, 164.81, 196.00, 220.00, 246.94, 261.63, 293.66, 329.63, 349.23,
    # Row 1
    196.00, 220.00, 246.94, 261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88,
    # Row 2
    261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25, 587.33, 659.26,
    # Row 3
    329.63, 349.23, 392.00, 440.00, 493.88, 523.25, 587.33, 659.26, 698.46, 783.99,
    # Row 4
    392.00, 440.00, 493.88, 523.25, 587.33, 659.26, 698.46, 783.99, 880.00, 987.77,
    # Row 5
    523.25, 587.33, 659.26, 698.46, 783.99, 880.00, 987.77, 1046.50, 1174.66, 1318.51,
    # Row 6
    659.26, 698.46, 783.99, 880.00, 987.77, 1046.50, 1174.66, 1318.51, 1396.91, 1567.98,
    # Row 7
    783.99, 880.00, 987.77, 1046.50, 1174.66, 1318.51, 1396.91, 1567.98, 1760.00, 1975.53,
    # Row 8
    987.77, 1046.50, 1174.66, 1318.51, 1396.91, 1567.98, 1760.00, 1975.53, 2093.00, 2349.32,
    # Row 9 (top) - high octave
    1174.66, 1318.51, 1396.91, 1567.98, 1760.00, 1975.53, 2093.00, 2349.32, 2637.02, 2793.83,
]

SAMPLE_RATE = 8000
SINE_TABLE_SIZE = 256
NUM_GRID_X = 10
NUM_GRID_Y = 10
NUM_TONES = NUM_GRID_X * NUM_GRID_Y
IMG_WIDTH = 100
IMG_HEIGHT = 100
NUM_AUDIO_SAMPLES = 16000  # 2 seconds at 8000 Hz

# Low-pass filter coefficient table dimensions
NUM_LPF_CUTOFFS = 16     # log-spaced from 200 Hz to 3800 Hz
NUM_LPF_RESONANCES = 8   # Q from 0.5 to 8.0
LPF_CUTOFF_MIN = 200.0
LPF_CUTOFF_MAX = 3800.0
LPF_Q_MIN = 0.5
LPF_Q_MAX = 8.0
FRAC_BITS = 15
Q15_SCALE = (1 << FRAC_BITS) - 1  # 32767


def generate_sine_table():
    """Generate a 256-entry Q15 sine lookup table."""
    table = []
    for i in range(SINE_TABLE_SIZE):
        val = math.sin(2.0 * math.pi * i / SINE_TABLE_SIZE)
        q15 = int(round(val * 32767))
        q15 = max(-32767, min(32767, q15))
        table.append(q15)
    return table


def compute_phase_increments():
    """Compute phase increments for each tone frequency.

    Phase accumulator is uint32_t, wraps at 2^32.
    Table index = (phase >> 24) & 0xFF for 256-entry table.
    phase_inc = freq * 2^32 / sample_rate
    """
    incs = []
    for freq in TONE_FREQS:
        inc = int(round(freq * (2**32) / SAMPLE_RATE))
        incs.append(inc & 0xFFFFFFFF)
    return incs


def generate_test_image():
    """Generate a synthetic 100x100 grayscale test image.

    Creates a pattern with varying brightness regions to produce
    interesting audio output - diagonal gradient with some circular features.
    """
    pixels = []
    cx, cy = IMG_WIDTH // 2, IMG_HEIGHT // 2
    for y in range(IMG_HEIGHT):
        for x in range(IMG_WIDTH):
            # Base: diagonal gradient
            base = int((x + y) * 255 / (IMG_WIDTH + IMG_HEIGHT - 2))

            # Add circular bright spot
            dx = x - cx
            dy = y - cy
            dist = math.sqrt(dx * dx + dy * dy)
            circle = max(0, int(200 * (1.0 - dist / (IMG_WIDTH * 0.4))))

            # Add some grid-like variation
            grid = 50 if ((x // 20) + (y // 20)) % 2 == 0 else 0

            val = min(255, base + circle + grid)
            pixels.append(val)
    return pixels


def compute_lpf_coefficients():
    """Precompute biquad low-pass filter coefficients for a grid of
    cutoff frequencies and Q (resonance) values.

    Uses the Audio EQ Cookbook formulas (Robert Bristow-Johnson):
        w0 = 2*pi*fc/Fs
        alpha = sin(w0)/(2*Q)
        b0 = (1 - cos(w0))/2
        b1 = 1 - cos(w0)
        b2 = (1 - cos(w0))/2
        a0 = 1 + alpha
        a1 = -2*cos(w0)
        a2 = 1 - alpha
    All normalized by a0, then scaled to Q15.

    Returns a flat list: [cutoff0_q0_b0, _b1, _b2, _a1, _a2, cutoff0_q1_b0, ...]
    """
    coeffs = []
    cutoffs = []
    q_values = []

    # Log-spaced cutoff frequencies
    log_min = math.log(LPF_CUTOFF_MIN)
    log_max = math.log(LPF_CUTOFF_MAX)
    for i in range(NUM_LPF_CUTOFFS):
        t = i / (NUM_LPF_CUTOFFS - 1) if NUM_LPF_CUTOFFS > 1 else 0
        fc = math.exp(log_min + t * (log_max - log_min))
        cutoffs.append(fc)

    # Log-spaced Q values
    log_qmin = math.log(LPF_Q_MIN)
    log_qmax = math.log(LPF_Q_MAX)
    for j in range(NUM_LPF_RESONANCES):
        t = j / (NUM_LPF_RESONANCES - 1) if NUM_LPF_RESONANCES > 1 else 0
        q = math.exp(log_qmin + t * (log_qmax - log_qmin))
        q_values.append(q)

    for fc in cutoffs:
        for q in q_values:
            w0 = 2.0 * math.pi * fc / SAMPLE_RATE
            alpha = math.sin(w0) / (2.0 * q)
            cos_w0 = math.cos(w0)

            b0 = (1.0 - cos_w0) / 2.0
            b1 = 1.0 - cos_w0
            b2 = (1.0 - cos_w0) / 2.0
            a0 = 1.0 + alpha
            a1 = -2.0 * cos_w0
            a2 = 1.0 - alpha

            # Normalize by a0
            b0 /= a0
            b1 /= a0
            b2 /= a0
            a1 /= a0
            a2 /= a0

            # Scale to Q15
            coeffs.append(int(round(b0 * Q15_SCALE)))
            coeffs.append(int(round(b1 * Q15_SCALE)))
            coeffs.append(int(round(b2 * Q15_SCALE)))
            coeffs.append(int(round(a1 * Q15_SCALE)))
            coeffs.append(int(round(a2 * Q15_SCALE)))

    return coeffs, cutoffs, q_values


def write_array(f, arr, typename, per_line=10):
    """Write a C array literal."""
    for i, v in enumerate(arr):
        if i != 0:
            f.write(", ")
        if i % per_line == 0 and i != 0:
            f.write("\n    ")
        f.write(str(v))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output", required=True, help="Output .h file")
    parser.add_argument("--num-samples", type=int, default=NUM_AUDIO_SAMPLES)
    args = parser.parse_args()

    sine_table = generate_sine_table()
    phase_incs = compute_phase_increments()
    test_image = generate_test_image()
    lpf_coeffs, lpf_cutoffs, lpf_q_values = compute_lpf_coefficients()

    with open(args.output, "w") as f:
        f.write("/* Auto-generated by gen_data.py - do not edit */\n")
        f.write("#include <stdint.h>\n\n")

        # Image dimensions
        f.write(f"#define IMG_WIDTH {IMG_WIDTH}\n")
        f.write(f"#define IMG_HEIGHT {IMG_HEIGHT}\n")
        f.write(f"#define NUM_GRID_X {NUM_GRID_X}\n")
        f.write(f"#define NUM_GRID_Y {NUM_GRID_Y}\n")
        f.write(f"#define NUM_TONES {NUM_TONES}\n")
        f.write(f"#define SINE_TABLE_SIZE {SINE_TABLE_SIZE}\n")
        f.write(f"#define SAMPLE_RATE {SAMPLE_RATE}\n")
        f.write(f"#define NUM_AUDIO_SAMPLES {args.num_samples}\n\n")

        # LPF table dimensions
        f.write(f"#define NUM_LPF_CUTOFFS {NUM_LPF_CUTOFFS}\n")
        f.write(f"#define NUM_LPF_RESONANCES {NUM_LPF_RESONANCES}\n")
        f.write(f"#define LPF_COEFFS_PER_ENTRY 5  /* b0, b1, b2, a1, a2 */\n\n")

        f.write(f"#ifdef DEFINE_CHANGO_DATA\n\n")

        # Test image (grayscale, 0-255)
        f.write(f"const uint8_t test_image[{IMG_WIDTH * IMG_HEIGHT}] = {{\n    ")
        write_array(f, test_image, "uint8_t", per_line=20)
        f.write("\n};\n\n")

        # Sine lookup table (Q15)
        f.write(f"const int16_t sine_table[{SINE_TABLE_SIZE}] = {{\n    ")
        write_array(f, sine_table, "int16_t", per_line=10)
        f.write("\n};\n\n")

        # Phase increments per tone
        f.write(f"const uint32_t phase_incs[{NUM_TONES}] = {{\n    ")
        write_array(f, phase_incs, "uint32_t", per_line=5)
        f.write("\n};\n\n")

        # LPF coefficient table
        # Layout: lpf_coeffs[cutoff_idx * NUM_LPF_RESONANCES * 5 + q_idx * 5 + coeff]
        total_coeffs = NUM_LPF_CUTOFFS * NUM_LPF_RESONANCES * 5
        f.write(f"/* Biquad LPF coefficients: Q15-scaled {{b0, b1, b2, a1, a2}}\n")
        f.write(f" * Indexed as: lpf_coeffs[(cutoff_idx * {NUM_LPF_RESONANCES} + q_idx) * 5 + coeff]\n")
        f.write(f" * Cutoffs ({NUM_LPF_CUTOFFS}): ")
        f.write(", ".join(f"{fc:.0f}" for fc in lpf_cutoffs))
        f.write(f" Hz\n")
        f.write(f" * Q values ({NUM_LPF_RESONANCES}): ")
        f.write(", ".join(f"{q:.2f}" for q in lpf_q_values))
        f.write(f"\n */\n")
        f.write(f"const int32_t lpf_coeffs[{total_coeffs}] = {{\n    ")
        write_array(f, lpf_coeffs, "int32_t", per_line=5)
        f.write("\n};\n\n")

        f.write("#endif /* DEFINE_CHANGO_DATA */\n")

    print(f"Generated {args.output}")
    print(f"  Image: {IMG_WIDTH}x{IMG_HEIGHT} grayscale")
    print(f"  Tones: {NUM_TONES} ({NUM_GRID_X}x{NUM_GRID_Y} grid)")
    print(f"  Sine table: {SINE_TABLE_SIZE} entries (Q15)")
    print(f"  LPF table: {NUM_LPF_CUTOFFS} cutoffs x {NUM_LPF_RESONANCES} Q values")
    print(f"    Cutoffs: {lpf_cutoffs[0]:.0f} - {lpf_cutoffs[-1]:.0f} Hz")
    print(f"    Q range: {lpf_q_values[0]:.2f} - {lpf_q_values[-1]:.2f}")
    print(f"  Audio: {args.num_samples} samples at {SAMPLE_RATE} Hz "
          f"({args.num_samples / SAMPLE_RATE:.1f}s)")


if __name__ == "__main__":
    main()
