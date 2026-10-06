"""Draws the MobiMic icon: a phone standing in a microphone cradle.

Writes assets/icon.png (1024 px), assets/icon.ico (Windows, all sizes) and the
copies the website uses. Run again after changing the colours here.

    python plugin/tools/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
S = 2048  # drawn large, then scaled down for smooth edges

SILVER_TOP, SILVER_BOTTOM = (253, 253, 255), (226, 227, 234)
PINK_A, PINK_B = (236, 56, 130), (255, 150, 190)   # deep rose -> soft pink
STAND = (214, 47, 116)


def gradient(size, start, end, horizontal=0.0):
    """A linear gradient, top to bottom, tilted towards the right by `horizontal` (0..1)."""
    w, h = size
    ramp = Image.linear_gradient("L").resize((w, h))                      # top -> bottom
    if horizontal:
        side = Image.linear_gradient("L").rotate(90, expand=True).resize((w, h))
        ramp = Image.blend(ramp, side, horizontal)
    return Image.composite(Image.new("RGB", size, end), Image.new("RGB", size, start), ramp)


def shape(draw_fn):
    mask = Image.new("L", (S, S), 0)
    draw_fn(ImageDraw.Draw(mask))
    return mask


def build():
    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Silver tile with a soft edge and a faint pink glow rising from the bottom.
    tile = shape(lambda d: d.rounded_rectangle((64, 64, S - 64, S - 64), 440, fill=255))
    icon.paste(gradient((S, S), SILVER_TOP, SILVER_BOTTOM), (0, 0), tile)

    glow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(glow).ellipse((S * 0.1, S * 0.62, S * 0.9, S * 1.25), fill=70)
    glow = ImageChops.multiply(glow.filter(ImageFilter.GaussianBlur(170)), tile)
    icon.paste(Image.new("RGB", (S, S), PINK_B), (0, 0), glow)

    edge = ImageChops.subtract(tile, shape(lambda d: d.rounded_rectangle((70, 70, S - 70, S - 70), 434, fill=255)))
    icon.paste(Image.new("RGB", (S, S), (205, 206, 214)), (0, 0), edge)

    cx = S // 2
    body = (cx - 250, 400, cx + 250, 1240)          # the phone
    cradle = (cx - 400, 720, cx + 400, 1520)        # circle whose lower half holds it

    def stand(d):
        d.arc(cradle, 0, 180, fill=255, width=78)
        for x in (cradle[0] + 39, cradle[2] - 39):  # round the ends of the arc
            d.ellipse((x - 39, 1120 - 39, x + 39, 1120 + 39), fill=255)
        d.rounded_rectangle((cx - 39, 1500, cx + 39, 1700), 39, fill=255)
        d.rounded_rectangle((cx - 230, 1640, cx + 230, 1718), 39, fill=255)

    # Soft shadow under the phone so it lifts off the tile.
    shadow = shape(lambda d: d.rounded_rectangle((body[0], body[1] + 40, body[2], body[3] + 40), 130, fill=120))
    icon.paste(Image.new("RGB", (S, S), (150, 30, 85)), (0, 0), shadow.filter(ImageFilter.GaussianBlur(46)))

    icon.paste(Image.new("RGB", (S, S), STAND), (0, 0), shape(stand))
    icon.paste(gradient((S, S), PINK_A, PINK_B, horizontal=0.45), (0, 0),
               shape(lambda d: d.rounded_rectangle(body, 130, fill=255)))

    # Earpiece slit and a light sheen, the two details that make it read as a phone.
    icon.paste(Image.new("RGB", (S, S), (255, 228, 239)), (0, 0),
               shape(lambda d: d.rounded_rectangle((cx - 80, 478, cx + 80, 512), 17, fill=235)))
    sheen = shape(lambda d: d.rounded_rectangle((body[0] + 34, body[1] + 34, cx - 30, body[3] - 300), 100, fill=38))
    icon.paste(Image.new("RGB", (S, S), (255, 255, 255)), (0, 0), sheen.filter(ImageFilter.GaussianBlur(30)))

    return icon.resize((1024, 1024), Image.LANCZOS)


def main():
    icon = build()
    assets = ROOT / "assets"
    assets.mkdir(exist_ok=True)
    icon.save(assets / "icon.png", optimize=True)
    icon.save(assets / "icon.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    icon.resize((256, 256), Image.LANCZOS).save(ROOT / "docs" / "images" / "icon.png", optimize=True)
    icon.resize((64, 64), Image.LANCZOS).save(ROOT / "docs" / "favicon.png", optimize=True)
    icon.resize((128, 128), Image.LANCZOS).save(ROOT / "plugin" / "assets" / "icon.png", optimize=True)
    # The installer's corner image must be a BMP, so it sits on white.
    corner = Image.new("RGB", (1024, 1024), "white")
    corner.paste(icon, (0, 0), icon)
    corner.resize((147, 147), Image.LANCZOS).save(ROOT / "installer" / "wizard-small.bmp")
    print("wrote the icon files")


if __name__ == "__main__":
    main()
