#!/usr/bin/env python3
"""
Extract chango audio from UART output and write raw PCM to stdout.

Reads UART log from stdin (or a file argument), finds the
CHANGO_AUDIO_START / CHANGO_AUDIO_END markers, decodes the decimal
samples, and writes 16-bit signed little-endian PCM to stdout.

Usage:
    # From a saved UART log:
    python3 uart_to_raw.py < uart_log.txt > chango_output.raw
    play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw

    # Pipe directly from UART capture to SoX:
    cat /dev/ttyUSB0 | python3 uart_to_raw.py | play -t raw -r 8000 -e signed -b 16 -c 1 -

    # Or from the sim binary:
    ./chango_sim | python3 uart_to_raw.py > chango_output.raw
"""

import sys
import struct


def main():
    if len(sys.argv) > 1:
        inp = open(sys.argv[1], "r")
    else:
        inp = sys.stdin

    in_audio = False
    sample_rate = 0
    count = 0

    for line in inp:
        line = line.strip()

        if line.startswith("CHANGO_AUDIO_START"):
            parts = line.split()
            if len(parts) >= 2:
                sample_rate = int(parts[1])
            in_audio = True
            print(f"[uart_to_raw] Receiving audio at {sample_rate} Hz...",
                  file=sys.stderr)
            continue

        if line == "CHANGO_AUDIO_END":
            in_audio = False
            print(f"[uart_to_raw] Done: {count} samples "
                  f"({count / sample_rate:.1f}s at {sample_rate} Hz)",
                  file=sys.stderr)
            continue

        if in_audio:
            try:
                val = int(line)
                # Clamp to int16 range
                val = max(-32768, min(32767, val))
                sys.stdout.buffer.write(struct.pack("<h", val))
                count += 1
            except ValueError:
                # Skip non-numeric lines (other printf output)
                pass

    if inp is not sys.stdin:
        inp.close()


if __name__ == "__main__":
    main()
