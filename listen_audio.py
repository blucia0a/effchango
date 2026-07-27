#!/usr/bin/env python3
"""
Host-side listener for effchango's UART audio stream.

The firmware (main_hm0360.c -> audio_out_swil.c) writes one byte per sample:
8-bit UNSIGNED PCM, mono, nominally 8000 Hz, with no framing, no header and no
escaping. Every byte on the wire is a sample, so the stream can be joined at any
point -- there is nothing to synchronize to.

The default baud is 929370, which is what the firmware switches to during camera
bring-up (the PMU clock boost re-bases the UART clock). Read at that rate with
pyserial; the kernel tty layer tends to drop bytes at non-standard baud rates,
so avoid `cat`.

Modes:
    --measure           report the achieved sample rate and signal statistics
    --wav FILE          record to a .wav file
    --raw FILE          record headerless raw bytes ('-' for stdout)
    --play              play live through SoX (needs `play` on PATH)

Examples:
    python listen_audio.py /dev/eff-console --measure
    python listen_audio.py /dev/eff-console --wav out.wav --seconds 10
    python listen_audio.py /dev/eff-console --play
    python listen_audio.py /dev/eff-console --raw - | \
        play -t raw -r 8000 -e unsigned -b 8 -c 1 -

Note on rate: the firmware has no timer pacing it (the camera's clock boost
rules out the mtimer), so it emits samples as fast as the capture+synthesis loop
produces them. That is close to 8000 Hz by construction but not locked to it.
Play at 8000 for the pitch the tone table was designed around, or pass --rate
with what --measure reports to trade correct pitch for correct duration.
"""

import argparse
import math
import os
import shutil
import subprocess
import sys
import time
import wave

BAUD = 929370
RATE = 8000


def open_port(path, baud, timeout):
    import serial
    return serial.Serial(path, baudrate=baud, timeout=timeout)


def measure(port, settle, sample_bytes):
    """Report the stream's actual byte (== sample) rate.

    Discards `settle` seconds first so start-up and any partially buffered data
    do not skew the result, then times exactly `sample_bytes` bytes.
    """
    print(f"discarding {settle:.1f}s to settle...")
    t_end = time.monotonic() + settle
    while time.monotonic() < t_end:
        port.read(4096)

    print(f"timing {sample_bytes} bytes...")
    got = bytearray()
    t0 = time.monotonic()
    while len(got) < sample_bytes:
        chunk = port.read(min(4096, sample_bytes - len(got)))
        if not chunk:
            print("error: stream stalled", file=sys.stderr)
            return 1
        got.extend(chunk)
    elapsed = time.monotonic() - t0

    rate = len(got) / elapsed
    print(f"\n{len(got)} bytes in {elapsed:.3f}s")
    print(f"sample rate : {rate:.0f} Hz  ({rate / RATE:.3f} x the {RATE} Hz design rate)")

    semitones = 12 * math.log2(rate / RATE)
    print(f"pitch error : {semitones:+.2f} semitones if played at {RATE} Hz")

    lo, hi = min(got), max(got)
    mean = sum(got) / len(got)
    var = sum((b - mean) ** 2 for b in got) / len(got)
    print(f"signal      : min={lo} max={hi} mean={mean:.1f} rms_dev={var ** 0.5:.1f}")
    if lo == 0 or hi == 255:
        print("              (hitting the rails -- the mix is clipping)")
    if var ** 0.5 < 2:
        print("              (nearly flat -- is the lens covered?)")
    return 0


def record(port, seconds, wav_path, raw_path, rate):
    """Capture `seconds` of audio to a .wav and/or a raw file."""
    want = int(seconds * rate)
    got = bytearray()
    t0 = time.monotonic()
    while len(got) < want:
        chunk = port.read(min(4096, want - len(got)))
        if not chunk:
            print("error: stream stalled", file=sys.stderr)
            break
        got.extend(chunk)
        done = len(got) / want
        print(f"\r  {done * 100:5.1f}%  {len(got)} bytes", end="", flush=True)
    elapsed = time.monotonic() - t0
    print()

    if raw_path == "-":
        sys.stdout.buffer.write(bytes(got))
        sys.stdout.buffer.flush()
    elif raw_path:
        with open(raw_path, "wb") as f:
            f.write(bytes(got))
        print(f"wrote {raw_path} ({len(got)} bytes)")

    if wav_path:
        # wave writes signed 16-bit; convert from the wire's unsigned 8-bit.
        pcm16 = bytearray()
        for b in got:
            v = (b - 128) << 8
            pcm16 += (v & 0xFFFF).to_bytes(2, "little", signed=False)
        with wave.open(wav_path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(bytes(pcm16))
        print(f"wrote {wav_path} ({len(got)} samples @ {rate} Hz)")

    if elapsed > 0:
        print(f"captured at {len(got) / elapsed:.0f} Hz over {elapsed:.2f}s")
    return 0


def resolve_audio_driver(explicit):
    """Pick SoX's output driver.

    SoX defaults to ALSA, which does not exist under WSL -- there is no /dev/snd,
    only WSLg's PulseAudio server. Detect that and select pulseaudio so --play
    works without the caller having to know. Needs a SoX built with the
    pulseaudio driver (check `sox --help | grep 'AUDIO DEVICE DRIVERS'`).
    """
    if explicit:
        return explicit
    if os.environ.get("AUDIODRIVER"):
        return None                      # caller already chose; inherit it
    if os.path.exists("/mnt/wslg/PulseServer"):
        return "pulseaudio"
    return None


def play(port, rate, audio_driver):
    """Stream live to SoX's `play`."""
    if not shutil.which("play"):
        print("error: `play` (SoX) not found on PATH", file=sys.stderr)
        return 1
    cmd = ["play", "-q", "-t", "raw", "-r", str(rate),
           "-e", "unsigned", "-b", "8", "-c", "1", "-"]

    env = dict(os.environ)
    driver = resolve_audio_driver(audio_driver)
    if driver:
        env["AUDIODRIVER"] = driver
    shown = env.get("AUDIODRIVER", "SoX default")
    print(f"playing at {rate} Hz via {shown} -- Ctrl-C to stop")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, env=env)
    try:
        while True:
            chunk = port.read(1024)
            if not chunk:
                continue
            proc.stdin.write(chunk)
            proc.stdin.flush()
    except KeyboardInterrupt:
        pass
    except BrokenPipeError:
        print("player exited", file=sys.stderr)
    finally:
        try:
            proc.stdin.close()
        except (OSError, BrokenPipeError):
            pass
        proc.wait()
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="Listen to effchango's 8-bit unsigned PCM UART stream",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("port", nargs="?", default="/dev/eff-console",
                    help="serial device (default: /dev/eff-console)")
    ap.add_argument("--baud", type=int, default=BAUD,
                    help=f"wire baud rate (default: {BAUD})")
    ap.add_argument("--rate", type=int, default=RATE,
                    help=f"playback/output sample rate (default: {RATE})")
    ap.add_argument("--measure", action="store_true",
                    help="report the stream's actual sample rate and exit")
    ap.add_argument("--settle", type=float, default=1.5,
                    help="seconds to discard before measuring (default: 1.5)")
    ap.add_argument("--measure-bytes", type=int, default=80000,
                    help="bytes to time when measuring (default: 80000)")
    ap.add_argument("--wav", help="record to this .wav file")
    ap.add_argument("--raw", help="record raw bytes here ('-' for stdout)")
    ap.add_argument("--seconds", type=float, default=10.0,
                    help="seconds to record (default: 10)")
    ap.add_argument("--play", action="store_true",
                    help="play live via SoX")
    ap.add_argument("--audio-driver",
                    help="SoX output driver (e.g. pulseaudio, alsa). Autodetected "
                         "as pulseaudio under WSL, where there is no ALSA device.")
    args = ap.parse_args()

    if not (args.measure or args.wav or args.raw or args.play):
        ap.error("pick one of --measure, --wav, --raw or --play")

    try:
        port = open_port(args.port, args.baud, timeout=5.0)
    except Exception as e:            # serial.SerialException, ImportError, ...
        print(f"error opening {args.port}: {e}", file=sys.stderr)
        return 1

    # Drop whatever accumulated in the driver buffer while we were not looking,
    # so timings and recordings start from live data.
    port.reset_input_buffer()

    try:
        if args.measure:
            return measure(port, args.settle, args.measure_bytes)
        if args.play:
            return play(port, args.rate, args.audio_driver)
        return record(port, args.seconds, args.wav, args.raw, args.rate)
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
