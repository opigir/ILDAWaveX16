#!/usr/bin/env python3

# Rotating 5-point star pattern via IWP
# Usage: python iwp-star.py --ip 192.168.0.209

import argparse
import math
import socket
import struct
import time

IW_TYPE_0 = 0x00
IW_TYPE_1 = 0x01
IW_TYPE_3 = 0x03

UDP_PORT = 7200

CENTER = 0x8000
RADIUS_OUTER = 0x6000
RADIUS_INNER = 0x2600  # ~0.382 * outer for a regular star
POINTS_PER_SEGMENT = 8  # interpolated points per line segment
BLANK_DWELL = 3  # dwell points at each blanked move


def star_vertices(angle_offset: float):
    """Generate 10 vertices of a 5-point star (alternating outer/inner)."""
    verts = []
    for i in range(10):
        a = angle_offset + math.pi * 2 * i / 10 - math.pi / 2
        r = RADIUS_OUTER if i % 2 == 0 else RADIUS_INNER
        x = CENTER + int(r * math.cos(a))
        y = CENTER + int(r * math.sin(a))
        verts.append((x, y))
    return verts


def interpolate(x0, y0, x1, y1, steps):
    """Linear interpolation between two points."""
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = int(x0 + (x1 - x0) * t)
        y = int(y0 + (y1 - y0) * t)
        pts.append((x, y))
    return pts


def build_star_frame(angle_offset: float, r: int, g: int, b: int):
    """Build a complete star frame with interpolation and blanking."""
    verts = star_vertices(angle_offset)
    verts.append(verts[0])  # close the shape

    frame = []

    # Dwell at start with blank
    x0, y0 = verts[0]
    for _ in range(BLANK_DWELL):
        frame.append((x0, y0, 0, 0, 0))

    for i in range(len(verts) - 1):
        x0, y0 = verts[i]
        x1, y1 = verts[i + 1]
        seg = interpolate(x0, y0, x1, y1, POINTS_PER_SEGMENT)
        for sx, sy in seg:
            frame.append((sx, sy, r, g, b))

    # Dwell at end
    xn, yn = verts[-1]
    for _ in range(BLANK_DWELL):
        frame.append((xn, yn, 0, 0, 0))

    return frame


def pack_frame(frame):
    """Pack frame points into IWP Type 3 packet bytes."""
    return b''.join(
        struct.pack('>BHHHHH', IW_TYPE_3, x, y, r, g, b)
        for x, y, r, g, b in frame
    )


def main():
    ap = argparse.ArgumentParser(description="Rotating star pattern via IWP")
    ap.add_argument("--ip", required=True, help="Projector IP address")
    ap.add_argument("--scan", type=int, default=20000, help="Scan rate Hz (default 20000)")
    ap.add_argument("--rpm", type=float, default=30, help="Rotation speed in RPM (default 30)")
    ap.add_argument("--color", default="00ffff", help="Hex color (default 00ffff)")
    args = ap.parse_args()

    # Parse color
    c = args.color.lstrip('#')
    r = int(c[0:2], 16) << 8 | int(c[0:2], 16)
    g = int(c[2:4], 16) << 8 | int(c[2:4], 16)
    b = int(c[4:6], 16) << 8 | int(c[4:6], 16)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.ip, UDP_PORT)

    # Set scan period
    scan_period = max(1, int(1_000_000 / args.scan))
    sock.sendto(struct.pack('>BI', IW_TYPE_1, scan_period), target)
    time.sleep(0.01)

    rads_per_sec = args.rpm * 2 * math.pi / 60
    frame_interval = 1.0 / 60  # ~60 updates/sec

    print(f"Sending star to {args.ip}:{UDP_PORT}")
    print(f"Scan rate: {args.scan} Hz, rotation: {args.rpm} RPM, color: #{c}")
    print("Ctrl+C to stop")

    t0 = time.monotonic()
    try:
        while True:
            t = time.monotonic() - t0
            angle = t * rads_per_sec
            frame = build_star_frame(angle, r, g, b)
            packet = pack_frame(frame)
            sock.sendto(packet, target)
            time.sleep(frame_interval)
    except KeyboardInterrupt:
        # Send blank
        sock.sendto(struct.pack('>B', IW_TYPE_0), target)
        print("\nStopped")


if __name__ == "__main__":
    main()
