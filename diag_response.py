#!/usr/bin/env python3
"""
Diagnostic: does the audio actually respond to the scene?

Prints a live level meter off the UART audio stream, one line per window, so you
can cover / uncover / move the camera and watch the amplitude follow. If the
instrument is working, covering the lens should collapse the level toward zero;
a level that barely moves means the image is not reaching the synthesizer (or the
filter is overflowing -- see "Biquad int32 overflow" in CLAUDE.md).

Also writes the captured bytes so the run can be re-analyzed afterwards.

Usage:
    python diag_response.py /dev/eff-console --seconds 25 --raw run.raw
"""

import argparse
import sys
import time


def main():
    ap = argparse.ArgumentParser(description="Live level meter for the audio stream")
    ap.add_argument("port", nargs="?", default="/dev/eff-console")
    ap.add_argument("--baud", type=int, default=929370)
    ap.add_argument("--seconds", type=float, default=25.0)
    ap.add_argument("--window", type=int, default=2000,
                    help="samples per reported window (default: 2000, ~0.25s)")
    ap.add_argument("--raw", help="also write the raw bytes here")
    args = ap.parse_args()

    import serial
    port = serial.Serial(args.port, baudrate=args.baud, timeout=5.0)
    port.reset_input_buffer()

    print(f"{args.seconds:.0f}s level meter -- cover / uncover / move the camera now")
    print(f"{'t':>6}  {'rms':>5}  {'min':>4} {'max':>4}  level")

    allbytes = bytearray()
    t0 = time.monotonic()
    try:
        while time.monotonic() - t0 < args.seconds:
            chunk = port.read(args.window)
            if not chunk:
                print("stream stalled", file=sys.stderr)
                break
            allbytes.extend(chunk)
            # Wire format is unsigned 8-bit centred on 128.
            dev = [b - 128 for b in chunk]
            rms = (sum(d * d for d in dev) / len(dev)) ** 0.5
            bar = "#" * min(60, int(rms))
            print(f"{time.monotonic() - t0:6.2f}  {rms:5.1f}  "
                  f"{min(chunk):4d} {max(chunk):4d}  {bar}")
    except KeyboardInterrupt:
        pass
    finally:
        port.close()

    if args.raw and allbytes:
        with open(args.raw, "wb") as f:
            f.write(bytes(allbytes))
        print(f"\nwrote {args.raw} ({len(allbytes)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
