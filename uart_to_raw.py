#!/usr/bin/env python3
"""
Extract chango audio from UART output and write raw PCM to stdout.

Reads UART log from stdin (or a file argument), decodes decimal
sample values, and writes 16-bit signed little-endian PCM to stdout.

Robust to lost UART data: if CHANGO_AUDIO_START/END markers are
missing, any line that is a bare integer is treated as a sample.

Processes lines incrementally so it works with streaming input
(e.g., cat /dev/ttyUSB0).

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

    sample_rate = 8000  # default if START marker is lost
    count = 0
    buf = []

    # Flush every 256 samples (~32ms at 8kHz) to balance latency vs choppiness
    FLUSH_SAMPLES = 4096 

    while True:
        line = inp.readline()
        if not line:
            break
        line = line.strip()
        if not line:
            continue

        if line.startswith("CHANGO_AUDIO_START"):
            parts = line.split()
            if len(parts) >= 2:
                sample_rate = int(parts[1])
            print(f"[uart_to_raw] Receiving audio at {sample_rate} Hz...",
                  file=sys.stderr, flush=True)
            continue

        if line == "CHANGO_AUDIO_END":
            duration = f"{count / sample_rate:.1f}s" if sample_rate else "?s"
            print(f"[uart_to_raw] Done: {count} samples "
                  f"({duration} at {sample_rate} Hz)",
                  file=sys.stderr, flush=True)
            continue

        # Try to parse any line as a sample — robust to missing markers
        try:
            val = int(line)
            if -32768 <= val <= 32767:
                buf.append(struct.pack("<h", val))
                count += 1
                if len(buf) >= FLUSH_SAMPLES:
                    sys.stdout.buffer.write(b"".join(buf))
                    sys.stdout.buffer.flush()
                    buf.clear()
        except ValueError:
            pass

    # Flush remaining samples
    if buf:
        sys.stdout.buffer.write(b"".join(buf))
        sys.stdout.buffer.flush()

    if count > 0 and sample_rate > 0:
        print(f"[uart_to_raw] Total: {count} samples "
              f"({count / sample_rate:.1f}s at {sample_rate} Hz)",
              file=sys.stderr, flush=True)

    if inp is not sys.stdin:
        inp.close()


if __name__ == "__main__":
    main()
