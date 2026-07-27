#!/usr/bin/env python3
"""
Headless companion to view.py: grab N camera frames and report on them.

Same wire format as view.py -- each frame is the 4-byte SOF magic 0xDEADBEEF
followed by width*height raw grayscale bytes. Instead of opening a window this
prints per-frame brightness/contrast statistics and writes PNGs, so a camera
build can be checked over a terminal or from a script.

Usage:
    python grab_frames.py /dev/eff-console --baud 929370 -n 5 -o /tmp/frames
"""

import argparse
import os
import sys

import numpy as np
import serial

SOF_MAGIC = b"\xDE\xAD\xBE\xEF"


def read_exact(port, n):
    """Read exactly n bytes, looping until all arrive."""
    buf = bytearray()
    while len(buf) < n:
        chunk = port.read(n - len(buf))
        if not chunk:
            raise TimeoutError(f"timed out: got {len(buf)}/{n} bytes")
        buf.extend(chunk)
    return bytes(buf)


def find_sof(port):
    """Slide a 4-byte window over the stream until the SOF magic appears.

    Returns the bytes that were skipped, which is useful for spotting boot
    banners or a baud mismatch.
    """
    skipped = bytearray()
    window = bytearray(read_exact(port, len(SOF_MAGIC)))
    while bytes(window) != SOF_MAGIC:
        skipped.append(window[0])
        window[:-1] = window[1:]
        window[-1] = read_exact(port, 1)[0]
    return bytes(skipped)


def main():
    ap = argparse.ArgumentParser(description="Headless HM0360 frame grabber")
    ap.add_argument("port", help="serial device, e.g. /dev/eff-console")
    ap.add_argument("width", type=int, nargs="?", default=160)
    ap.add_argument("height", type=int, nargs="?", default=120)
    ap.add_argument("--baud", type=int, default=929370)
    ap.add_argument("-n", "--num-frames", type=int, default=5)
    ap.add_argument("-o", "--outdir", default="frames",
                    help="directory for the PNGs (default: ./frames)")
    ap.add_argument("--timeout", type=float, default=15.0,
                    help="per-read serial timeout in seconds")
    ap.add_argument("--flip", action="store_true",
                    help="flip vertically on the host (firmware already "
                         "un-flips the hat's inverted sensor)")
    args = ap.parse_args()

    npix = args.width * args.height
    os.makedirs(args.outdir, exist_ok=True)

    print(f"opening {args.port} at {args.baud} baud, "
          f"{args.width}x{args.height} ({npix} B/frame)")
    port = serial.Serial(args.port, baudrate=args.baud, timeout=args.timeout)

    try:
        for i in range(args.num_frames):
            skipped = find_sof(port)
            if skipped:
                # Printable leading bytes are almost always a boot banner;
                # a wall of binary usually means the baud rate is wrong.
                text = skipped.decode("ascii", "replace").strip()
                print(f"  [skipped {len(skipped)} B before SOF: {text!r}]")

            frame = np.frombuffer(read_exact(port, npix), dtype=np.uint8)
            frame = frame.reshape((args.height, args.width))
            if args.flip:
                frame = frame[::-1]

            path = os.path.join(args.outdir, f"frame{i:03d}.png")
            # Written via numpy->cv2 only for the encoder; no display involved.
            import cv2
            cv2.imwrite(path, frame)

            print(f"frame {i}: mean={frame.mean():6.2f} std={frame.std():6.2f} "
                  f"min={frame.min():3d} max={frame.max():3d} -> {path}")
    except (TimeoutError, serial.SerialException) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        pass
    finally:
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
