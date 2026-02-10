#!/usr/bin/env python3
"""Generate deterministic Forest Floor app icon assets (.icns and .ico)."""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct
import zlib


def _png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    payload = chunk_type + data
    crc = binascii.crc32(payload) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + payload + struct.pack(">I", crc)


def write_png_rgba(path: pathlib.Path, width: int, height: int) -> None:
    """Write a simple stylized icon as RGBA PNG."""
    center_x = (width - 1) / 2.0
    center_y = (height - 1) / 2.0
    radius = min(width, height) * 0.43
    inner = radius * 0.62

    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type: None
        for x in range(width):
            dx = x - center_x
            dy = y - center_y
            dist = (dx * dx + dy * dy) ** 0.5

            # Warm background gradient.
            t = y / max(height - 1, 1)
            r = int(18 + (42 - 18) * t)
            g = int(31 + (84 - 31) * t)
            b = int(27 + (54 - 27) * t)

            # Outer ring.
            if inner < dist < radius:
                r, g, b = 215, 147, 54

            # Inner disc.
            if dist <= inner:
                falloff = max(0.0, min(1.0, 1.0 - dist / max(inner, 1.0)))
                r = int(46 + 120 * falloff)
                g = int(74 + 78 * falloff)
                b = int(64 + 44 * falloff)

            # Four quadrant pips as a drum-machine nod.
            pip = min(width, height) * 0.08
            offsets = (
                (-inner * 0.46, -inner * 0.46),
                (inner * 0.46, -inner * 0.46),
                (-inner * 0.46, inner * 0.46),
                (inner * 0.46, inner * 0.46),
            )
            for ox, oy in offsets:
                pdx = dx - ox
                pdy = dy - oy
                if pdx * pdx + pdy * pdy <= pip * pip:
                    r, g, b = 245, 229, 184
                    break

            rows.extend((r, g, b, 255))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(rows), level=9)
    png = b"\x89PNG\r\n\x1a\n"
    png += _png_chunk(b"IHDR", ihdr)
    png += _png_chunk(b"IDAT", idat)
    png += _png_chunk(b"IEND", b"")
    path.write_bytes(png)


def write_ico_with_png(path: pathlib.Path, png_path: pathlib.Path) -> None:
    png = png_path.read_bytes()
    # ICONDIR: reserved, type=1(icon), count=1
    header = struct.pack("<HHH", 0, 1, 1)
    # ICONDIRENTRY: width=0(256), height=0(256), color_count, reserved,
    #               planes=1, bit_count=32, bytes_in_res, image_offset
    entry = struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, len(png), 6 + 16)
    path.write_bytes(header + entry + png)


def write_icns(path: pathlib.Path, png_by_type: dict[str, bytes]) -> None:
    chunks = bytearray()
    for icon_type in ("icp4", "icp5", "icp6", "ic07", "ic08", "ic09", "ic10"):
        png = png_by_type[icon_type]
        chunks.extend(icon_type.encode("ascii"))
        chunks.extend(struct.pack(">I", 8 + len(png)))
        chunks.extend(png)
    total_size = 8 + len(chunks)
    path.write_bytes(b"icns" + struct.pack(">I", total_size) + bytes(chunks))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate Forest Floor icon assets.")
    parser.add_argument(
        "--output-dir",
        default="assets/icons",
        help="Directory where generated icons are written.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = pathlib.Path(args.output_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    source_1024 = out_dir / "forest-floor-1024.png"
    source_512 = out_dir / "forest-floor-512.png"
    source_256 = out_dir / "forest-floor-256.png"
    source_128 = out_dir / "forest-floor-128.png"
    source_64 = out_dir / "forest-floor-64.png"
    source_32 = out_dir / "forest-floor-32.png"
    source_16 = out_dir / "forest-floor-16.png"
    icns = out_dir / "forest-floor.icns"
    ico = out_dir / "forest-floor.ico"

    write_png_rgba(source_1024, 1024, 1024)
    write_png_rgba(source_512, 512, 512)
    write_png_rgba(source_256, 256, 256)
    write_png_rgba(source_128, 128, 128)
    write_png_rgba(source_64, 64, 64)
    write_png_rgba(source_32, 32, 32)
    write_png_rgba(source_16, 16, 16)
    write_icns(
        icns,
        {
            "icp4": source_16.read_bytes(),
            "icp5": source_32.read_bytes(),
            "icp6": source_64.read_bytes(),
            "ic07": source_128.read_bytes(),
            "ic08": source_256.read_bytes(),
            "ic09": source_512.read_bytes(),
            "ic10": source_1024.read_bytes(),
        },
    )
    write_ico_with_png(ico, source_256)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
