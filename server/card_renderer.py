"""Render the exact 240x280, 4-bit grayscale card consumed by the ESP32."""

from __future__ import annotations

from functools import lru_cache
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

WIDTH = 240
HEIGHT = 280
PACKED_BYTES = WIDTH * HEIGHT // 2
SCALE = 3
DEFAULT_FONT = Path(__file__).resolve().parent / "fonts" / "NotoSansSC-Regular.otf"


@lru_cache(maxsize=24)
def _font(path: str, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(path, size * SCALE)


def _wrap(text: str, font: ImageFont.FreeTypeFont, draw: ImageDraw.ImageDraw) -> list[str]:
    max_width = 204 * SCALE
    lines: list[str] = []
    for paragraph in text.split("\n"):
        if not paragraph:
            lines.append("")
            continue
        line = ""
        for char in paragraph:
            candidate = line + char
            if line and draw.textlength(candidate, font=font) > max_width:
                lines.append(line)
                line = char
            else:
                line = candidate
        lines.append(line)
    return lines


def _layout(text: str, draw: ImageDraw.ImageDraw, font_path: Path) -> tuple[ImageFont.FreeTypeFont, list[str], int]:
    for size in (25, 23, 21, 19, 18, 17, 16):
        font = _font(str(font_path), size)
        lines = _wrap(text, font, draw)
        line_step = round(size * 1.30 * SCALE)
        if len(lines) * line_step <= 184 * SCALE:
            return font, lines, line_step
    raise ValueError("内容无法排进屏幕，请减少换行或文字")


def _image(text: str, font_path: Path) -> Image.Image:
    canvas = Image.new("L", (WIDTH * SCALE, HEIGHT * SCALE), 0)
    draw = ImageDraw.Draw(canvas)
    title_font = _font(str(font_path), 14)
    hint_font = _font(str(font_path), 12)
    draw.text((18 * SCALE, 15 * SCALE), "留言", font=title_font, fill=170, anchor="lt")
    draw.line((18 * SCALE, 48 * SCALE, 222 * SCALE, 48 * SCALE), fill=59, width=SCALE)
    draw.text((120 * SCALE, 252 * SCALE), "轻点返回", font=hint_font, fill=145, anchor="mt")
    if text:
        font, lines, line_step = _layout(text, draw, font_path)
        total_height = len(lines) * line_step
        start_y = 64 * SCALE + max(0, (180 * SCALE - total_height) // 2)
        for index, line in enumerate(lines):
            draw.text((18 * SCALE, start_y + index * line_step), line, font=font, fill=247, anchor="lt")
    return canvas.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)


def pack_image(image: Image.Image) -> bytes:
    """High nibble is the left pixel; each nibble maps 0..15 to 0..255."""
    if image.size != (WIDTH, HEIGHT):
        raise ValueError("card must be exactly 240x280")
    pixels = image.convert("L").tobytes()
    out = bytearray(PACKED_BYTES)
    for i in range(PACKED_BYTES):
        left = min(15, (pixels[2 * i] + 8) // 17)
        right = min(15, (pixels[2 * i + 1] + 8) // 17)
        out[i] = (left << 4) | right
    return bytes(out)


def unpack_image(packed: bytes) -> Image.Image:
    if len(packed) != PACKED_BYTES:
        raise ValueError("invalid packed card length")
    pixels = bytearray(WIDTH * HEIGHT)
    for i, value in enumerate(packed):
        pixels[2 * i] = (value >> 4) * 17
        pixels[2 * i + 1] = (value & 15) * 17
    return Image.frombytes("L", (WIDTH, HEIGHT), bytes(pixels))


def render_card(text: str, font_path: Path = DEFAULT_FONT) -> bytes:
    if not font_path.is_file():
        raise FileNotFoundError(f"CJK font missing: {font_path}")
    return pack_image(_image(text, font_path))


def packed_to_png(packed: bytes) -> bytes:
    stream = BytesIO()
    unpack_image(packed).save(stream, format="PNG", optimize=True)
    return stream.getvalue()
