#!/usr/bin/env python3

# Scrolling text via IWP
# Usage: python iwp-text.py --ip 192.168.0.209 --text "HELLO WORLD"

import argparse
import math
import socket
import struct
import time

IW_TYPE_0 = 0x00
IW_TYPE_1 = 0x01
IW_TYPE_3 = 0x03

UDP_PORT = 7200
POINT_SIZE = 11
MAX_PACKET = 1023
MAX_POINTS_PER_PACKET = MAX_PACKET // POINT_SIZE

# Stroke font on a 5-wide, 7-tall grid.
# Each character is a list of strokes (polylines).
# Each stroke is a list of (x, y) tuples. y=0 is top, y=7 is bottom.
# Between strokes the beam blanks.
FONT = {
    'A': [[(0,7),(0,2),(1,0),(2,0),(3,0),(4,2),(4,7)], [(0,4),(4,4)]],
    'B': [[(0,7),(0,0),(3,0),(4,1),(4,2),(3,3),(0,3)], [(3,3),(4,4),(4,6),(3,7),(0,7)]],
    'C': [[(4,1),(3,0),(1,0),(0,1),(0,6),(1,7),(3,7),(4,6)]],
    'D': [[(0,7),(0,0),(3,0),(4,1),(4,6),(3,7),(0,7)]],
    'E': [[(4,0),(0,0),(0,3),(3,3)], [(0,3),(0,7),(4,7)]],
    'F': [[(4,0),(0,0),(0,3),(3,3)], [(0,3),(0,7)]],
    'G': [[(4,1),(3,0),(1,0),(0,1),(0,6),(1,7),(3,7),(4,6),(4,4),(2,4)]],
    'H': [[(0,0),(0,7)], [(4,0),(4,7)], [(0,4),(4,4)]],
    'I': [[(1,0),(3,0)], [(2,0),(2,7)], [(1,7),(3,7)]],
    'J': [[(1,0),(4,0)], [(3,0),(3,6),(2,7),(1,7),(0,6)]],
    'K': [[(0,0),(0,7)], [(4,0),(0,4),(4,7)]],
    'L': [[(0,0),(0,7),(4,7)]],
    'M': [[(0,7),(0,0),(2,3),(4,0),(4,7)]],
    'N': [[(0,7),(0,0),(4,7),(4,0)]],
    'O': [[(1,0),(3,0),(4,1),(4,6),(3,7),(1,7),(0,6),(0,1),(1,0)]],
    'P': [[(0,7),(0,0),(3,0),(4,1),(4,3),(3,4),(0,4)]],
    'Q': [[(1,0),(3,0),(4,1),(4,6),(3,7),(1,7),(0,6),(0,1),(1,0)], [(3,6),(4,7)]],
    'R': [[(0,7),(0,0),(3,0),(4,1),(4,3),(3,4),(0,4)], [(2,4),(4,7)]],
    'S': [[(4,1),(3,0),(1,0),(0,1),(0,2),(1,3),(3,4),(4,5),(4,6),(3,7),(1,7),(0,6)]],
    'T': [[(0,0),(4,0)], [(2,0),(2,7)]],
    'U': [[(0,0),(0,6),(1,7),(3,7),(4,6),(4,0)]],
    'V': [[(0,0),(2,7),(4,0)]],
    'W': [[(0,0),(1,7),(2,4),(3,7),(4,0)]],
    'X': [[(0,0),(4,7)], [(4,0),(0,7)]],
    'Y': [[(0,0),(2,3),(4,0)], [(2,3),(2,7)]],
    'Z': [[(0,0),(4,0),(0,7),(4,7)]],
    '0': [[(1,0),(3,0),(4,1),(4,6),(3,7),(1,7),(0,6),(0,1),(1,0)], [(0,6),(4,1)]],
    '1': [[(1,1),(2,0),(2,7)], [(1,7),(3,7)]],
    '2': [[(0,1),(1,0),(3,0),(4,1),(4,3),(0,7),(4,7)]],
    '3': [[(0,1),(1,0),(3,0),(4,1),(4,2),(3,3),(2,3)], [(3,3),(4,4),(4,6),(3,7),(1,7),(0,6)]],
    '4': [[(0,0),(0,4),(4,4)], [(3,0),(3,7)]],
    '5': [[(4,0),(0,0),(0,3),(3,3),(4,4),(4,6),(3,7),(1,7),(0,6)]],
    '6': [[(3,0),(1,0),(0,1),(0,6),(1,7),(3,7),(4,6),(4,4),(3,3),(0,3)]],
    '7': [[(0,0),(4,0),(2,7)]],
    '8': [[(1,0),(3,0),(4,1),(4,2),(3,3),(1,3),(0,2),(0,1),(1,0)],
          [(1,3),(0,4),(0,6),(1,7),(3,7),(4,6),(4,4),(3,3)]],
    '9': [[(4,4),(4,1),(3,0),(1,0),(0,1),(0,3),(1,4),(4,4),(4,6),(3,7),(1,7)]],
    ' ': [],
    '.': [[(2,6),(2,7)]],
    ',': [[(2,6),(1,7)]],
    '!': [[(2,0),(2,5)], [(2,7),(2,7)]],
    '?': [[(0,1),(1,0),(3,0),(4,1),(4,2),(3,3),(2,3),(2,5)], [(2,7),(2,7)]],
    '-': [[(1,3),(3,3)]],
    '+': [[(0,3),(4,3)], [(2,1),(2,5)]],
    ':': [[(2,2),(2,2)], [(2,6),(2,6)]],
    '/': [[(0,7),(4,0)]],
    '(': [[(3,0),(1,2),(1,5),(3,7)]],
    ')': [[(1,0),(3,2),(3,5),(1,7)]],
    '#': [[(1,0),(1,7)], [(3,0),(3,7)], [(0,2),(4,2)], [(0,5),(4,5)]],
    '&': [[(4,7),(1,3),(1,1),(2,0),(3,0),(4,1),(3,2),(0,5),(0,6),(1,7),(3,7),(4,5)]],
    '*': [[(0,1),(4,5)], [(4,1),(0,5)], [(2,0),(2,6)]],
    "'": [[(2,0),(2,2)]],
    '"': [[(1,0),(1,2)], [(3,0),(3,2)]],
}

CHAR_WIDTH = 5
CHAR_HEIGHT = 7
CHAR_SPACING = 1.5  # gap between characters in grid units
BLANK_DWELL = 3


def clamp16(v):
    return max(0, min(0xFFFF, v))


def text_to_strokes(text):
    """Convert text string to a list of strokes in grid coordinates."""
    text = text.upper()
    strokes = []
    x_offset = 0

    for ch in text:
        glyph = FONT.get(ch, FONT.get('?', []))
        for stroke in glyph:
            strokes.append([(x + x_offset, y) for x, y in stroke])
        x_offset += CHAR_WIDTH + CHAR_SPACING

    total_width = x_offset - CHAR_SPACING if text else 0
    return strokes, total_width


def interpolate_stroke(stroke, pts_per_unit=2):
    """Interpolate along a stroke polyline."""
    result = []
    for i in range(len(stroke) - 1):
        x0, y0 = stroke[i]
        x1, y1 = stroke[i + 1]
        dist = math.hypot(x1 - x0, y1 - y0)
        steps = max(1, int(dist * pts_per_unit))
        for j in range(steps + (1 if i == len(stroke) - 2 else 0)):
            t = j / steps
            result.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
    if len(stroke) == 1:
        result.append(stroke[0])
    return result


BOX_FRAC = 0.15  # bounding box as fraction of display (calibrated)
BOX_W = int(0xFFFF * BOX_FRAC * (5 * (CHAR_WIDTH + CHAR_SPACING) - CHAR_SPACING) / CHAR_HEIGHT)
BOX_H = int(0xFFFF * BOX_FRAC)


def build_text_frame(text, x_scroll=0.0, r=0xFFFF, g=0xFFFF, b=0xFFFF,
                     scale=None, y_center=0x8000):
    """Build a frame of text, auto-scaled to fit the calibrated bounding box.
    x_scroll: horizontal offset in grid units (increases to scroll left).
    """
    strokes, total_width = text_to_strokes(text)
    if not strokes:
        return [], total_width

    # Auto-fit: scale to whichever dimension is tighter
    if scale is None:
        scale_w = BOX_W / total_width if total_width > 0 else 1
        scale_h = BOX_H / CHAR_HEIGHT
        scale = min(scale_w, scale_h)

    x_origin = 0x8000 - (total_width * scale) / 2
    y_origin = y_center - (CHAR_HEIGHT * scale) / 2

    frame = []
    prev_end = None

    for stroke in strokes:
        pts = interpolate_stroke(stroke)
        if not pts:
            continue

        # Transform to display coordinates with scroll offset
        transformed = []
        for gx, gy in pts:
            sx = clamp16(0xFFFF - int(x_origin + (gx - x_scroll) * scale))
            sy = clamp16(0xFFFF - int(y_origin + gy * scale))
            transformed.append((sx, sy))

        # Blank move from previous position
        if prev_end:
            for _ in range(BLANK_DWELL):
                frame.append((*prev_end, 0, 0, 0))
        for _ in range(BLANK_DWELL):
            frame.append((*transformed[0], 0, 0, 0))

        # Draw stroke
        for sx, sy in transformed:
            frame.append((sx, sy, r, g, b))

        prev_end = transformed[-1]

    # Final blank dwell
    if prev_end:
        for _ in range(BLANK_DWELL):
            frame.append((*prev_end, 0, 0, 0))

    return frame, total_width


def pack_frame(frame):
    packets = []
    for i in range(0, len(frame), MAX_POINTS_PER_PACKET):
        chunk = frame[i:i + MAX_POINTS_PER_PACKET]
        packets.append(b''.join(
            struct.pack('>BHHHHH', IW_TYPE_3, x, y, r, g, b)
            for x, y, r, g, b in chunk
        ))
    return packets


def main():
    ap = argparse.ArgumentParser(description="Scrolling text via IWP")
    ap.add_argument("--ip", required=True, help="Projector IP address")
    ap.add_argument("--text", default="HELLO WORLD", help="Text to display")
    ap.add_argument("--scan", type=int, default=25000, help="Scan rate Hz")
    ap.add_argument("--color", default="00ffff", help="Hex color")
    ap.add_argument("--scroll", action="store_true", help="Enable scrolling")
    ap.add_argument("--speed", type=float, default=5.0, help="Scroll speed (grid units/sec)")
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

    # Calculate repeats
    test_frame, total_width = build_text_frame(args.text, r=r, g=g, b=b)
    pts_per_frame = len(test_frame) if test_frame else 100
    update_hz = 30
    repeats = max(1, int(args.scan / (pts_per_frame * update_hz)) + 1)
    frame_interval = 1.0 / update_hz

    mode = "scrolling" if args.scroll else "static"
    print(f"Sending text to {args.ip}:{UDP_PORT}: \"{args.text}\"")
    print(f"Scan rate: {args.scan} Hz, color: #{c}, mode: {mode}")
    print(f"Points/frame: {pts_per_frame}, repeats: {repeats}")
    print("Ctrl+C to stop")

    t0 = time.monotonic()
    try:
        while True:
            if args.scroll:
                t = time.monotonic() - t0
                x_scroll = t * args.speed
                # Wrap around
                display_width_grid = 0xFFFF / ((0xFFFF * 0.6) / CHAR_HEIGHT)
                if x_scroll > total_width + display_width_grid:
                    t0 = time.monotonic()
            else:
                x_scroll = 0.0

            frame, _ = build_text_frame(args.text, x_scroll=x_scroll, r=r, g=g, b=b)
            if frame:
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
