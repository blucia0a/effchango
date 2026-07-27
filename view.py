#!/usr/bin/env python3
"""
Display 320x240 8-bit grayscale video from a serial device.

The device must prefix each frame with the 4-byte magic: 0xDE 0xAD 0xBE 0xEF
followed by 320*240 = 76800 raw pixel bytes, row-major.

Usage:
    python view.py /dev/ttyUSB0 --baud 460800
"""

import argparse
import queue
import sys
import threading

import cv2
import numpy as np
import serial

FRAME_W = 160
FRAME_H = 120
SOF_MAGIC = b'\xDE\xAD\xBE\xEF'

def find_frame_start(port: serial.Serial) -> None:
    """Scan the stream byte-by-byte until the SOF magic is found."""
    buf = bytearray(read_exact(port, len(SOF_MAGIC)))
    while bytes(buf) != SOF_MAGIC:
        buf[:-1] = buf[1:]
        b = b''
        while not b:
            b = port.read(1)
        buf[-1] = b[0]


def read_exact(port: serial.Serial, n: int) -> bytes:
    """Read exactly n bytes, looping until all arrive."""
    buf = bytearray()
    while len(buf) < n:
        chunk = port.read(n - len(buf))
        if not chunk:
            raise serial.SerialTimeoutException(f"Timed out: got {len(buf)}/{n} bytes")
        buf.extend(chunk)
    return bytes(buf)


def read_frame(port: serial.Serial) -> np.ndarray:
    """Block until a complete frame is received and return it as a numpy array."""
    find_frame_start(port)
    data = read_exact(port, FRAME_W * FRAME_H)
    return np.frombuffer(data, dtype=np.uint8).reshape((FRAME_H, FRAME_W))[::-1]
    #return np.frombuffer(data, dtype=np.uint8).reshape((FRAME_H, FRAME_W))


def log_frame(log_file, frame_count: int, frame: np.ndarray) -> None:
    log_file.write(f"--- Frame {frame_count} ---\n")
    for row in frame:
        log_file.write(" ".join(str(v) for v in row) + "\n")
    log_file.flush()


def reader_thread(port: serial.Serial, frame_queue: queue.Queue, stop: threading.Event,
                  log_file) -> None:
    frame_count = 0
    try:
        while not stop.is_set():
            frame = read_frame(port)
            frame_count += 1
            frame_queue.put((frame_count, frame))
            if log_file:
                log_frame(log_file, frame_count, frame)
    except serial.SerialTimeoutException:
        print("Timed out waiting for data.", file=sys.stderr)
    except Exception as e:
        print(f"Reader error: {e}", file=sys.stderr)
    finally:
        stop.set()


def main() -> None:
    global FRAME_W, FRAME_H
    parser = argparse.ArgumentParser(description="Serial grayscale video viewer")
    parser.add_argument("port", help="Serial port (e.g. /dev/ttyUSB0 or COM3)")
    parser.add_argument("width", type=int, nargs="?", default=FRAME_W)
    parser.add_argument("height", type=int, nargs="?", default=FRAME_H)
    parser.add_argument("--baud", type=int, default=460800,
                        help="Baud rate (default: 460800)")
    parser.add_argument("--log", help="File to log frames to (plaintext, appended)")
    args = parser.parse_args()

    FRAME_W = args.width
    FRAME_H = args.height

    print(f"Opening {args.port} at {args.baud} baud, {FRAME_W}x{FRAME_H}...")
    try:
        port = serial.Serial(args.port, baudrate=args.baud, timeout=None)
    except serial.SerialException as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    log_file = open(args.log, "a") if args.log else None
    if log_file:
        print(f"Logging frames to {args.log}")

    print("Waiting for frames... press Q to quit.")
    cv2.namedWindow("Camera", cv2.WINDOW_NORMAL)

    frame_queue: queue.Queue = queue.Queue(maxsize=1)
    stop = threading.Event()
    t = threading.Thread(target=reader_thread, args=(port, frame_queue, stop, log_file),
                         daemon=True)
    t.start()

    try:
        while not stop.is_set():
            try:
                frame_count, frame = frame_queue.get_nowait()
                print(f"Frame {frame_count}")
                cv2.imshow("Camera", frame)
            except queue.Empty:
                pass
            if cv2.waitKey(1) & 0xFF == ord('q'):
                stop.set()
    except KeyboardInterrupt:
        stop.set()
    finally:
        port.close()
        cv2.destroyAllWindows()
        if log_file:
            log_file.close()


if __name__ == "__main__":
    main()
