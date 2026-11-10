#!/usr/bin/env python3
"""Grab the current LCD framebuffer over the debug UART and save it as PNG.

The firmware answers 'S' with:  'SS' + w(u16 LE) + h(u16 LE) + len(u32 LE)
followed by len bytes of RLE triples [count][rgb565 lo][rgb565 hi].

    python tools/serial_shot.py --port COM4 --out menu.png
    python tools/serial_shot.py --port COM4 --cmd p --out play.png
    python tools/serial_shot.py --port COM4 --info

Needs pyserial only:  pip install pyserial
PNG encoding uses the standard library, so no Pillow is required.
"""
import argparse
import struct
import sys
import time
import zlib

def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, w, h, rgb888):
    stride = w * 3
    rows = b"".join(b"\x00" + rgb888[y * stride:(y + 1) * stride]
                    for y in range(h))
    head = struct.pack(">2I5B", w, h, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head)
           + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(png)


def read_exact(ser, n):
    out = bytearray()
    while len(out) < n:
        part = ser.read(n - len(out))
        if not part:
            raise TimeoutError("serial timeout while reading %d bytes" % n)
        out += part
    return bytes(out)


def grab(ser, timeout=15.0):
    """Find the 'SS' sync marker, then decode one frame."""
    end = time.time() + timeout
    while time.time() < end:
        if ser.read(1) != b"S":
            continue
        if ser.read(1) != b"S":
            continue
        hdr = read_exact(ser, 8)
        w, h, length = struct.unpack("<HHI", hdr)
        if not (0 < w <= 1024 and 0 < h <= 1024 and length <= w * h * 3):
            continue                      # false sync inside a log line
        payload = read_exact(ser, length)
        if len(payload) % 3:
            continue
        pixels = [0] * (w * h)
        pos = 0
        for i in range(0, len(payload), 3):
            count = payload[i]
            color = payload[i + 1] | (payload[i + 2] << 8)
            for _ in range(count):
                if pos < len(pixels):
                    pixels[pos] = color
                pos += 1
        if pos != len(pixels):
            continue
        rgb = bytearray(w * h * 3)
        for i, v in enumerate(pixels):
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            rgb[i * 3:i * 3 + 3] = bytes((r, g, b))
        return w, h, rgb
    raise TimeoutError("no frame received (is the firmware running?)")


def main():
    # Imported here so --help works without pyserial installed.
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required: pip install pyserial")

    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="shot.png")
    ap.add_argument("--cmd", default="", help="send these bytes before the shot, e.g. 'p' or 'nnn'")
    ap.add_argument("--info", action="store_true", help="print the firmware state instead of a PNG")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=2) as ser:
        ser.reset_input_buffer()
        if args.cmd:
            ser.write(args.cmd.encode())
            time.sleep(0.25)
        if args.info:
            ser.reset_input_buffer()
            ser.write(b"i")
            print(ser.readline().decode("utf-8", "replace").strip())
            return
        ser.reset_input_buffer()
        ser.write(b"S")
        w, h, rgb = grab(ser)
    write_png(args.out, w, h, rgb)
    print("saved %s (%dx%d)" % (args.out, w, h))


if __name__ == "__main__":
    main()
