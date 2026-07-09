#!/usr/bin/env python3
"""Read the badge's USB-Serial/JTAG console without toggling reset lines."""
import sys, time, glob, serial

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
pattern = sys.argv[2] if len(sys.argv) > 2 else '/dev/cu.usbmodem*'

port = None
deadline = time.time() + 15
while time.time() < deadline and not port:
    hits = sorted(glob.glob(pattern))
    if hits:
        port = hits[0]
    else:
        time.sleep(0.5)
if not port:
    sys.exit("no usbmodem port found")

# Steady DTR high (CDC gates TX on 'terminal present'), RTS low, never toggled.
def open_port():
    p = None
    while not p:
        hits = sorted(glob.glob(pattern))
        if hits:
            p = hits[0]
        else:
            time.sleep(0.3)
    s = serial.Serial()
    s.port = p
    s.baudrate = 115200
    s.dtr = True
    s.rts = False
    s.timeout = 0.5
    s.open()
    return s

s = open_port()
end = time.time() + seconds
buf = b''
while time.time() < end:
    try:
        buf += s.read(4096)
    except (serial.SerialException, OSError):
        try:
            s.close()
        except Exception:
            pass
        time.sleep(0.5)
        try:
            s = open_port()
        except Exception:
            time.sleep(0.5)
try:
    s.close()
except Exception:
    pass
out = buf.decode('utf-8', errors='replace')
print(out[-8000:] if len(out) > 8000 else out)
print(f"--- {len(buf)} bytes from {port} ---", file=sys.stderr)
