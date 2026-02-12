#!/usr/bin/env python3

# Rotating 3D wireframe cube via IWP
# Usage: python iwp-cube.py --ip 192.168.0.209

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
SCALE = 0x4000
POINTS_PER_EDGE = 10
BLANK_DWELL = 3

# Unit cube vertices centered at origin
VERTICES = [
    (-1, -1, -1),
    ( 1, -1, -1),
    ( 1,  1, -1),
    (-1,  1, -1),
    (-1, -1,  1),
    ( 1, -1,  1),
    ( 1,  1,  1),
    (-1,  1,  1),
]

# Edges as vertex index pairs - ordered to minimize blanking moves
# Draw bottom face, then top face, then verticals
EDGES = [
    (0, 1), (1, 2), (2, 3), (3, 0),  # back face
    (4, 5), (5, 6), (6, 7), (7, 4),  # front face
    (0, 4), (1, 5), (2, 6), (3, 7),  # connecting edges
]

# Optimal draw order: trace continuous paths where possible
# Back face loop -> jump to front face loop -> verticals
DRAW_ORDER = [
    (0, 1), (1, 2), (2, 3), (3, 0),  # back face (continuous)
    (0, 4),                            # vertical (continuous from v0)
    (4, 5), (5, 6), (6, 7), (7, 4),  # front face (continuous)
    (5, 1),                            # vertical back (continuous from v5)
    (2, 6),                            # jump to v2, vertical
    (3, 7),                            # jump to v3, vertical
]


def rotate_x(v, a):
    x, y, z = v
    c, s = math.cos(a), math.sin(a)
    return (x, y * c - z * s, y * s + z * c)


def rotate_y(v, a):
    x, y, z = v
    c, s = math.cos(a), math.sin(a)
    return (x * c + z * s, y, -x * s + z * c)


def rotate_z(v, a):
    x, y, z = v
    c, s = math.cos(a), math.sin(a)
    return (x * c - y * s, x * s + y * c, z)


def clamp16(v):
    return max(0, min(0xFFFF, v))


def project(v, fov=2.5):
    """Perspective projection."""
    x, y, z = v
    d = fov / (fov + z)
    return (clamp16(int(CENTER + x * SCALE * d)), clamp16(int(CENTER + y * SCALE * d)))


def interpolate(x0, y0, x1, y1, steps):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        pts.append((int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t)))
    return pts


def build_cube_frame(ax, ay, az, r, g, b):
    """Build a wireframe cube frame with rotation angles ax, ay, az."""
    # Transform vertices
    projected = []
    for v in VERTICES:
        v = rotate_x(v, ax)
        v = rotate_y(v, ay)
        v = rotate_z(v, az)
        projected.append(project(v))

    frame = []
    prev_end = None

    for (i0, i1) in DRAW_ORDER:
        x0, y0 = projected[i0]
        x1, y1 = projected[i1]

        # If start of this edge != end of last edge, blank move
        if prev_end is None or prev_end != (x0, y0):
            # Dwell blank at current position
            if prev_end:
                for _ in range(BLANK_DWELL):
                    frame.append((*prev_end, 0, 0, 0))
            # Blank move to start of new edge
            for _ in range(BLANK_DWELL):
                frame.append((x0, y0, 0, 0, 0))

        # Draw edge
        seg = interpolate(x0, y0, x1, y1, POINTS_PER_EDGE)
        for sx, sy in seg:
            frame.append((sx, sy, r, g, b))

        prev_end = (x1, y1)

    # Final dwell
    if prev_end:
        for _ in range(BLANK_DWELL):
            frame.append((*prev_end, 0, 0, 0))

    return frame


POINT_SIZE = 11  # 1 byte type + 5 * 2 bytes
MAX_PACKET = 1023
MAX_POINTS_PER_PACKET = MAX_PACKET // POINT_SIZE


def pack_frame(frame):
    """Pack frame into chunked packets that fit within UDP MTU."""
    packets = []
    for i in range(0, len(frame), MAX_POINTS_PER_PACKET):
        chunk = frame[i:i + MAX_POINTS_PER_PACKET]
        packets.append(b''.join(
            struct.pack('>BHHHHH', IW_TYPE_3, x, y, r, g, b)
            for x, y, r, g, b in chunk
        ))
    return packets


def main():
    ap = argparse.ArgumentParser(description="Rotating 3D cube via IWP")
    ap.add_argument("--ip", required=True, help="Projector IP address")
    ap.add_argument("--scan", type=int, default=25000, help="Scan rate Hz (default 25000)")
    ap.add_argument("--rpm", type=float, default=20, help="Rotation speed RPM (default 20)")
    ap.add_argument("--color", default="00ff40", help="Hex color (default 00ff40)")
    args = ap.parse_args()

    c = args.color.lstrip('#')
    r = int(c[0:2], 16) << 8 | int(c[0:2], 16)
    g = int(c[2:4], 16) << 8 | int(c[2:4], 16)
    b = int(c[4:6], 16) << 8 | int(c[4:6], 16)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.ip, UDP_PORT)

    scan_period = max(1, int(1_000_000 / args.scan))
    sock.sendto(struct.pack('>BI', IW_TYPE_1, scan_period), target)
    time.sleep(0.01)

    rads_per_sec = args.rpm * 2 * math.pi / 60

    # Calculate how many times to repeat each frame to keep the DAC fed.
    # A test frame gives us the point count; the DAC consumes at scan rate.
    test_frame = build_cube_frame(0, 0, 0, r, g, b)
    pts_per_frame = len(test_frame)
    update_hz = 30  # how often we recalculate the rotation angle
    repeats = max(1, int(args.scan / (pts_per_frame * update_hz)) + 1)
    frame_interval = 1.0 / update_hz

    print(f"Sending cube to {args.ip}:{UDP_PORT}")
    print(f"Scan rate: {args.scan} Hz, rotation: {args.rpm} RPM, color: #{c}")
    print(f"Points/frame: {pts_per_frame}, repeats: {repeats}, update: {update_hz} Hz")
    print("Ctrl+C to stop")

    t0 = time.monotonic()
    try:
        while True:
            t = time.monotonic() - t0
            ax = t * rads_per_sec * 0.7
            ay = t * rads_per_sec
            az = t * rads_per_sec * 0.3
            frame = build_cube_frame(ax, ay, az, r, g, b)
            packets = pack_frame(frame)
            for _ in range(repeats):
                for pkt in packets:
                    sock.sendto(pkt, target)
            time.sleep(frame_interval)
    except KeyboardInterrupt:
        sock.sendto(struct.pack('>B', IW_TYPE_0), target)
        print("\nStopped")


if __name__ == "__main__":
    main()
