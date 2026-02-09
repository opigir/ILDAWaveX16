# IWP (ILDA Wave Protocol) - Python Examples

## Protocol

IWP is a simple UDP protocol on port **7200** for streaming laser points to the ILDAWaveX16.

### Message Types

| Type | Bytes | Description |
|------|-------|-------------|
| `0x00` | 1 | Turn off / clear buffer |
| `0x01` | 5 | Set scan period: `>BI` (type + uint32 microseconds) |
| `0x02` | 8 | Point with 8-bit color: `>BHHBBB` (type + X + Y + R + G + B) |
| `0x03` | 11 | Point with 16-bit color: `>BHHHHH` (type + X + Y + R + G + B) |

All multi-byte values are **big-endian**. Multiple messages can be packed into a single UDP packet.

### Coordinate System

- X/Y: unsigned 16-bit (`0x0000` to `0xFFFF`), center at `0x8000`
- Color (Type 2): 8-bit per channel (0-255), mapped internally to 16-bit
- Color (Type 3): 16-bit per channel (0-65535)

### Scan Period

Set with Type 1. Value is in microseconds. To convert from Hz:
```python
scan_period_us = 1_000_000 // scan_rate_hz
```

## Example Scripts

### iwp-star.py - Rotating 5-Point Star

```
python iwp-star.py --ip 192.168.0.209
python iwp-star.py --ip 192.168.0.209 --scan 20000 --rpm 30 --color 00ffff
```

| Arg | Default | Description |
|-----|---------|-------------|
| `--ip` | (required) | Projector IP address |
| `--scan` | 20000 | Scan rate in Hz |
| `--rpm` | 30 | Rotation speed |
| `--color` | 00ffff | Hex RGB color |

### iwp-cube.py - Rotating 3D Wireframe Cube

```
python iwp-cube.py --ip 192.168.0.209
python iwp-cube.py --ip 192.168.0.209 --scan 25000 --rpm 20 --color 00ff40
```

| Arg | Default | Description |
|-----|---------|-------------|
| `--ip` | (required) | Projector IP address |
| `--scan` | 25000 | Scan rate in Hz |
| `--rpm` | 20 | Rotation speed |
| `--color` | 00ff40 | Hex RGB color |

Features perspective projection and optimized edge draw order to minimize blanking moves.

### iwp-text.py - Text Display

```
python iwp-text.py --ip 192.168.0.209 --text "HELLO"
python iwp-text.py --ip 192.168.0.209 --text "BEN & ELLA" --color ff00ff
python iwp-text.py --ip 192.168.0.209 --text "HELLO WORLD" --scroll --speed 8
```

| Arg | Default | Description |
|-----|---------|-------------|
| `--ip` | (required) | Projector IP address |
| `--text` | HELLO WORLD | Text to display |
| `--scan` | 25000 | Scan rate in Hz |
| `--color` | 00ffff | Hex RGB color |
| `--scroll` | off | Enable scrolling mode |
| `--speed` | 5.0 | Scroll speed (grid units/sec) |

Uses a built-in Hershey-style stroke font (A-Z, 0-9, punctuation). Text is auto-scaled to fit a calibrated bounding box - short strings appear larger, long strings shrink to fit. Coordinates are inverted in X and Y to match the galvo orientation.

### iwp-ilda.py - ILDA File Streamer

```
python iwp-ilda.py --file animation.ild --ip 192.168.0.209 --scan 1000
python iwp-ilda.py --file anim1.ild --ip 192.168.0.209 --scan 100000 --fps 0 --repeat 100
```

### iwp-gen.ipynb - Interactive Pattern Generator

Jupyter notebook with circle, sine wave, and manual point generators.

## Lessons Learned

### Packet size limit

UDP packets must be chunked to **1023 bytes max** (~93 points at 11 bytes each for Type 3). Larger packets are silently dropped over WiFi. The `iwp-ilda.py` reference implementation uses this limit. The star script (~105 points, ~1155 bytes) worked by luck near the edge; the cube (~156 points, ~1716 bytes) was silently dropped until chunked.

### Buffer fill rate

The firmware's ring buffer holds 8192 points. The DAC consumes points at the scan rate (e.g. 25,000 points/sec). If the sender doesn't supply points fast enough, the display will flicker or go blank. Calculate repeats to keep the buffer fed:

```python
repeats = max(1, scan_rate_hz // (points_per_frame * update_hz) + 1)
```

### Coordinate clamping

Projected coordinates (especially with perspective projection) can exceed the uint16 range. Always clamp to 0-65535 before packing:

```python
def clamp16(v):
    return max(0, min(0xFFFF, v))
```

### Blanking and dwell

When drawing disconnected line segments, insert **blanked dwell points** (RGB=0) at both the end of the current segment and the start of the next. This gives the galvos time to reposition without drawing stray lines. 3 dwell points works well.

### Display orientation

The galvo stage on this unit has inverted X and Y relative to the IWP coordinate space. The text script compensates with `0xFFFF - coord` on both axes. Other scripts (star, cube) are symmetric so it doesn't matter, but any asymmetric pattern (text, logos) needs the inversion.

### Auto-fit text scaling

The calibrated bounding box for text is 15% of display height. Auto-fit scales to whichever dimension (width or height) is tighter, so short text fills the height and long text shrinks to fit the width. This keeps all text within the safe scan area regardless of length.

### ESP32-S3 boot modes

After flashing via USB, the device may stay in download mode (`boot:0x0`). Press the RST button **without** holding BOOT to enter normal flash boot (`boot:0x8`).
