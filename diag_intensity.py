#!/usr/bin/env python3
"""
Diagnostic host side for an INTENSITY_DEBUG firmware build.

The firmware emits, per captured frame:
    AB CD EF 01                 magic
    16 bytes                    the 4x4 intensity grid fed to synthesize()
    frame[0], frame[centre]     two raw sensor pixels
    scaled[0], scaled[centre]   two pixels after scale_image()

Prints one line per frame so you can watch the grid track the scene. Cover the
lens: every intensity should collapse toward 0. If the raw sensor pixels move but
the intensities do not, the fault is in scale_image/pixelate rather than capture.

Usage:
    python diag_intensity.py /dev/eff-console --seconds 25
"""

import argparse
import sys
import time

MAGIC = b"\xAB\xCD\xEF\x01"
NUM_TONES = 16
PAYLOAD = NUM_TONES + 4


def read_exact(port, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = port.read(n - len(buf))
        if not chunk:
            raise TimeoutError(f"timed out: {len(buf)}/{n} bytes")
        buf.extend(chunk)
    return bytes(buf)


def find_magic(port):
    window = bytearray(read_exact(port, len(MAGIC)))
    while bytes(window) != MAGIC:
        window[:-1] = window[1:]
        window[-1] = read_exact(port, 1)[0]


def main():
    ap = argparse.ArgumentParser(description="Watch the chango intensity grid live")
    ap.add_argument("port", nargs="?", default="/dev/eff-console")
    ap.add_argument("--baud", type=int, default=929370)
    ap.add_argument("--seconds", type=float, default=25.0)
    args = ap.parse_args()

    import serial
    port = serial.Serial(args.port, baudrate=args.baud, timeout=8.0)
    port.reset_input_buffer()

    print(f"{args.seconds:.0f}s -- cover / uncover the lens now")
    print(f"{'t':>6}  {'mean':>5}  {'min':>4} {'max':>4}  | "
          f"{'raw px':>13} {'scaled px':>13}  | grid")

    seen = []
    t0 = time.monotonic()
    try:
        while time.monotonic() - t0 < args.seconds:
            find_magic(port)
            p = read_exact(port, PAYLOAD)
            grid, extra = list(p[:NUM_TONES]), list(p[NUM_TONES:])
            raw0, rawc, sc0, scc = extra
            mean = sum(grid) / len(grid)
            seen.append(tuple(grid))
            print(f"{time.monotonic() - t0:6.2f}  {mean:5.1f}  "
                  f"{min(grid):4d} {max(grid):4d}  | "
                  f"{raw0:3d},{rawc:3d}{'':7s} {sc0:3d},{scc:3d}{'':7s}  | "
                  + " ".join(f"{v:3d}" for v in grid))
    except (TimeoutError, KeyboardInterrupt) as e:
        print(f"stopped: {e}", file=sys.stderr)
    finally:
        port.close()

    if seen:
        distinct = len(set(seen))
        print(f"\n{len(seen)} frames, {distinct} distinct grids")
        if distinct == 1:
            print("VERDICT: the grid never changed -- pixelate output is static")
        else:
            allv = [v for g in seen for v in g]
            print(f"VERDICT: grid is live (values spanned {min(allv)}..{max(allv)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
