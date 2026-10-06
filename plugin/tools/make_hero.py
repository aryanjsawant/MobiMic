"""Composes docs/images/hero.png (the README banner) from the generated screenshots."""
from pathlib import Path
from PIL import Image, ImageDraw

IMAGES = Path(__file__).resolve().parents[2] / "docs" / "images"
W, H, PAD = 1600, 900, 70


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width - 1, img.height - 1), radius, fill=255)
    out = Image.new("RGBA", img.size)
    out.paste(img.convert("RGBA"), (0, 0), mask)
    return out


def fit(name, height):
    img = Image.open(IMAGES / name)
    return img.resize((round(img.width * height / img.height), height), Image.LANCZOS)


canvas = Image.new("RGBA", (W, H), "#181b21")
draw = ImageDraw.Draw(canvas)

# Phone: the page inside a simple bezel.
screen = rounded(fit("phone-live.png", H - 2 * PAD - 24), 34)
bezel = Image.new("RGBA", (screen.width + 24, screen.height + 24))
ImageDraw.Draw(bezel).rounded_rectangle((0, 0, bezel.width - 1, bezel.height - 1), 46, fill="#2b2f37", outline="#3c4043", width=2)
bezel.alpha_composite(screen, (12, 12))

# Plugin window with a thin border.
plugin = rounded(fit("plugin-capturing.png", H - 2 * PAD), 16)
frame = Image.new("RGBA", (plugin.width + 4, plugin.height + 4))
ImageDraw.Draw(frame).rounded_rectangle((0, 0, frame.width - 1, frame.height - 1), 18, fill="#3c4043")
frame.alpha_composite(plugin, (2, 2))

gap = 240
left = (W - bezel.width - gap - frame.width) // 2
canvas.alpha_composite(bezel, (left, (H - bezel.height) // 2))
canvas.alpha_composite(frame, (left + bezel.width + gap, PAD - 2))

# Three "signal" arcs travelling from the phone to the plugin.
cx, cy = left + bezel.width + 50, H // 2
for i, r in enumerate((40, 80, 120)):
    draw.arc((cx - r, cy - r, cx + r, cy + r), -38, 38, fill=(52, 199, 89, 255 - i * 60), width=8)

canvas.convert("RGB").save(IMAGES / "hero.png", optimize=True)
print("wrote", IMAGES / "hero.png")
