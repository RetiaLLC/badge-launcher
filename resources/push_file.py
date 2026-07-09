#!/usr/bin/env python3
"""Push a file to the badge's SD card via the launcher's serial-push protocol.

Usage: push_file.py <port> <local_file> <badge_path>
e.g.:  push_file.py /dev/cu.usbmodem1101 doom.bin /sd/firmware/doom.app.bin

DTR is held steadily asserted (never toggled) to avoid the badge's
native-USB download-mode trap. Requires the launcher to be running.
"""
import base64
import sys
import time

import serial

CHUNK = 384  # bytes per base64 line (512 b64 chars)


def open_port(path):
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.dtr = True
    s.rts = False
    s.timeout = 5
    s.open()
    return s


def read_line(s, deadline_s=10):
    end = time.time() + deadline_s
    buf = b""
    while time.time() < end:
        b = s.read(1)
        if not b:
            continue
        if b == b"\n":
            line = buf.decode(errors="replace").strip()
            if line:
                return line
            buf = b""
        else:
            buf += b
    return None


def main():
    port, local, remote = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(local, "rb").read()
    s = open_port(port)
    s.reset_input_buffer()

    s.write(b"PING\n")
    s.flush()
    if read_line(s) != "PONG":
        sys.exit("no PONG — is the launcher running?")

    s.write(f"PUT {remote} {len(data)}\n".encode())
    s.flush()
    resp = read_line(s)
    if resp != "READY":
        sys.exit(f"badge said: {resp}")

    # The badge acks every 8 lines with "K" (its USB rx ring drops on overflow).
    t0 = time.time()
    nlines = 0
    for i in range(0, len(data), CHUNK):
        s.write(base64.b64encode(data[i:i + CHUNK]) + b"\n")
        nlines += 1
        if nlines % 8 == 0:
            s.flush()
            if read_line(s, deadline_s=30) != "K":
                sys.exit("\nlost sync (no K ack)")
            if nlines % 512 == 0:
                pct = 100 * i // max(len(data), 1)
                print(f"\r{pct}% ", end="", flush=True)
    s.write(b"END\n")
    s.flush()

    resp = read_line(s, deadline_s=60)
    dt = time.time() - t0
    print(f"\r{resp}  ({len(data)} bytes in {dt:.1f}s, {len(data)/dt/1024:.0f} KB/s)")
    s.close()
    if not (resp or "").startswith("OK"):
        sys.exit(1)


if __name__ == "__main__":
    main()
