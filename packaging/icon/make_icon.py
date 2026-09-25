#!/usr/bin/env python3
"""Generates the MotionVR Bridge app icon and its platform formats.

    python3 packaging/icon/make_icon.py

Writes icon.png (1024 px master) plus:
  ui/icon.png                            window icon
  packaging/windows/MotionVRBridge.ico   exe and installer icon
  packaging/macos/MotionVRBridge.icns    app bundle icon (needs macOS iconutil)
  packaging/linux/motionvrbridge.png     desktop icon

Requires Pillow. Letters use Inter (SIL OFL 1.1, see Inter-LICENSE.txt).
"""

import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SIZE = 1024
SCALE = 2  # draw at 2x, then downsample for clean edges
S = SIZE * SCALE

BG_TOP = (21, 28, 44)
BG_BOTTOM = (8, 11, 20)
CYAN = (34, 211, 238)  # Theme.accent in ui/app.slint
PINK = (244, 114, 182)  # Theme.hot in ui/app.slint
WHITE = (248, 250, 252)

# Each letter is nudged off the baseline and slightly rotated so the word
# looks caught mid-motion.
LETTERS = [
    # glyph, dy (fraction of icon), rotation (degrees)
    ("M", -0.035, 2.0),
    ("V", 0.045, -3.0),
    ("B", -0.012, 1.5),
]


def rounded_background() -> Image.Image:
    # macOS-style body: 824 px square inside the 1024 canvas.
    margin = int(S * 100 / 1024)
    radius = int(S * 185 / 1024)
    gradient = Image.new("RGB", (S, S))
    for y in range(S):
        t = y / (S - 1)
        gradient.paste(tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)), (0, y, S, y + 1))

    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((margin, margin, S - margin, S - margin), radius, fill=255)

    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # soft drop shadow
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        (margin, margin + S * 0.012, S - margin, S - margin + S * 0.012), radius, fill=(0, 0, 0, 120))
    icon.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(S * 0.012)))
    body = gradient.convert("RGBA")
    body.putalpha(mask)
    icon.alpha_composite(body)
    return icon


def letter_layer(glyph: str, font: ImageFont.FreeTypeFont, color, rotation: float) -> Image.Image:
    box = font.getbbox(glyph)
    w, h = box[2] - box[0], box[3] - box[1]
    pad = int(h * 0.3)
    layer = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((pad - box[0], pad - box[1]), glyph, font=font, fill=color)
    return layer.rotate(rotation, resample=Image.BICUBIC, expand=True)


def draw_letters(icon: Image.Image) -> None:
    font = ImageFont.truetype(str(HERE / "InterVariable.ttf"), int(S * 0.27))
    font.set_variation_by_axes([32, 900])  # optical size, weight

    # Lay out by glyph advance; each letter layer is then centered on its slot.
    widths = [font.getbbox(g)[2] - font.getbbox(g)[0] for g, _, _ in LETTERS]
    tracking = S * 0.01
    total = sum(widths) + tracking * (len(widths) - 1)
    x = (S - total) / 2
    cy = S / 2

    ghost = S * 0.014  # offset of the colour "ghosts"
    for (glyph, dy, rot), w in zip(LETTERS, widths):
        cx = x + w / 2
        white = letter_layer(glyph, font, WHITE, rot)
        left = cx - white.width / 2
        top = cy - white.height / 2 + dy * S
        for color, ox, oy in ((PINK, ghost, ghost * 0.6), (CYAN, -ghost, -ghost * 0.6)):
            icon.alpha_composite(letter_layer(glyph, font, color + (200,), rot), (int(left + ox), int(top + oy)))
        icon.alpha_composite(white, (int(left), int(top)))
        x += w + tracking


def write_icns(master: Image.Image, out: Path) -> None:
    if not shutil.which("iconutil"):
        print(f"skipping {out.name}: iconutil (macOS) not available")
        return
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "icon.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            master.resize((size, size), Image.LANCZOS).save(iconset / f"icon_{size}x{size}.png")
            master.resize((size * 2, size * 2), Image.LANCZOS).save(iconset / f"icon_{size}x{size}@2x.png")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(out)], check=True)


def main() -> None:
    icon = rounded_background()
    draw_letters(icon)
    master = icon.resize((SIZE, SIZE), Image.LANCZOS)

    master.save(HERE / "icon.png")
    master.resize((256, 256), Image.LANCZOS).save(ROOT / "ui" / "icon.png")
    (ROOT / "packaging" / "linux").mkdir(parents=True, exist_ok=True)
    master.resize((512, 512), Image.LANCZOS).save(ROOT / "packaging" / "linux" / "motionvrbridge.png")
    master.save(ROOT / "packaging" / "windows" / "MotionVRBridge.ico",
                sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    (ROOT / "packaging" / "macos").mkdir(parents=True, exist_ok=True)
    write_icns(master, ROOT / "packaging" / "macos" / "MotionVRBridge.icns")
    print("icon written")


if __name__ == "__main__":
    main()
