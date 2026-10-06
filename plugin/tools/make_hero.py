"""Composes docs/images/hero.png (the banner for the README and website) from the generated screenshots.

    python plugin/tools/make_hero.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

IMAGES = Path(__file__).resolve().parents[2] / "docs" / "images"
W, H, PAD = 1600, 900, 76
SILVER_TOP, SILVER_BOTTOM, PINK, PINK_LIGHT = (251, 251, 253), (230, 231, 236), (236, 56, 130), (255, 150, 190)


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width - 1, img.height - 1), radius, fill=255)
    out = Image.new("RGBA", img.size)
    out.paste(img.convert("RGBA"), (0, 0), mask)
    return out


def fit(name, height):
    img = Image.open(IMAGES / name)
    return img.resize((round(img.width * height / img.height), height), Image.LANCZOS)


def place(canvas, item, position, radius):
    """Pastes `item` with a soft shadow underneath."""
    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    box = (position[0], position[1] + 18, position[0] + item.width, position[1] + item.height + 18)
    ImageDraw.Draw(shadow).rounded_rectangle(box, radius, fill=(40, 30, 60, 70))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(26)))
    canvas.alpha_composite(item, position)


# Background: silver, with the same pink glow as the product.
ramp = Image.linear_gradient("L").resize((W, H))
canvas = Image.composite(Image.new("RGB", (W, H), SILVER_BOTTOM), Image.new("RGB", (W, H), SILVER_TOP), ramp).convert("RGBA")
glow = Image.new("L", (W, H), 0)
ImageDraw.Draw(glow).ellipse((W - 760, -420, W + 260, 420), fill=120)
canvas.paste(Image.new("RGB", (W, H), PINK_LIGHT), (0, 0), glow.filter(ImageFilter.GaussianBlur(150)))

# Phone: the page inside a slim silver frame.
screen = rounded(fit("phone-live.png", H - 2 * PAD - 20), 40)
phone = Image.new("RGBA", (screen.width + 20, screen.height + 20))
ImageDraw.Draw(phone).rounded_rectangle((0, 0, phone.width - 1, phone.height - 1), 50, fill=(246, 246, 249), outline=(203, 204, 212), width=2)
phone.alpha_composite(screen, (10, 10))

window = rounded(fit("plugin-capturing.png", H - 2 * PAD), 22)

gap = 250
left = (W - phone.width - gap - window.width) // 2
place(canvas, phone, (left, (H - phone.height) // 2), 50)
place(canvas, window, (left + phone.width + gap, PAD), 22)

# Three "signal" arcs travelling from the phone to the plugin.
draw = ImageDraw.Draw(canvas)
cx, cy = left + phone.width + 58, H // 2
for i, r in enumerate((42, 84, 126)):
    colour = tuple(round(PINK[c] + (PINK_LIGHT[c] - PINK[c]) * i / 2) for c in range(3))
    draw.arc((cx - r, cy - r, cx + r, cy + r), -38, 38, fill=colour + (255,), width=9)

canvas.convert("RGB").save(IMAGES / "hero.png", optimize=True)
print("wrote", IMAGES / "hero.png")
