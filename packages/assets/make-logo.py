# Builds every logo asset from one geometry: logo-32/128/256/512.png, main.ico and logo.svg.
# Usage: python make-logo.py <output folder>   (needs Pillow). Units are a 1024 box.
import sys, os, random
from PIL import Image, ImageDraw, ImageFilter

SILVER_L = ("#F0F2F4", "#C4C9CF")
SILVER_D = ("#A2A9B1", "#5F656C")
BLUE, DEEP, SHADOW = "#3586E9", "#1B57C1", "#15449C"

TOP, BOT = 40, 984
STEM_X0, STEM_MID, STEM_X1 = 40, 191, 342
RIGHT = 936
UP_C = 709                 # upper arm upper edge: x + y = UP_C
BAND = 335                 # arm thickness measured vertically (~237 perpendicular)
LO_D2 = -70                # lower arm upper-right edge: y = x + LO_D2
LO_D1 = LO_D2 + BAND       # lower arm lower-left edge
END_R = 76                 # outer corner rounding of the arm ends
SOFT = 0.006               # softening of all other corners, fraction of size
SHADE = 64

UPPER = [(STEM_MID, UP_C - STEM_MID), (UP_C - TOP, TOP), (RIGHT, TOP),
         (RIGHT, UP_C + BAND - RIGHT), (STEM_MID, UP_C + BAND - STEM_MID)]
LOWER = [((UP_C - LO_D2) / 2, (UP_C + LO_D2) / 2), (RIGHT, RIGHT + LO_D2), (RIGHT, BOT),
         (BOT - LO_D1, BOT), ((UP_C - LO_D1) / 2, (UP_C + LO_D1) / 2)]
A = UP_C + BAND
SHADOW_BAND = [(-2000, A + 2000), (A + 2000, -2000), (A + SHADE + 2000, -2000), (-2000, A + SHADE + 2000)]

def hexrgb(h): return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))

def brushed(big, k, top_hex, bot_hex, x0, x1, seed):
    w, h = max(1, int((x1 - x0) * k)), max(1, int((BOT - TOP) * k))
    a, b = hexrgb(top_hex), hexrgb(bot_hex)
    rnd = random.Random(seed)
    cols = [rnd.uniform(-0.015, 0.015) for _ in range(w)]
    img = Image.new("RGBA", (w, h)); px = img.load()
    for y in range(h):
        f = y / max(1, h - 1)
        base = [a[c] + (b[c] - a[c]) * f for c in range(3)]
        for x in range(w):
            n = 1 + cols[x]
            px[x, y] = tuple(max(0, min(255, round(base[c] * n))) for c in range(3)) + (255,)
    full = Image.new("RGBA", (big, big), (0, 0, 0, 0)); full.paste(img, (int(x0 * k), int(TOP * k)))
    return full

def render(size, oversample=6):
    big = size * oversample; k = big / 1024.0
    S = lambda pts: [(x * k, y * k) for x, y in pts]
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    zero = Image.new("L", (big, big), 0)
    def mask(fn, soft=True):
        m = Image.new("L", (big, big), 0); fn(ImageDraw.Draw(m))
        if soft:
            m = m.filter(ImageFilter.GaussianBlur(big * SOFT)).point(lambda v: 255 if v >= 128 else 0)
        return m
    def paint(m, src):
        img.paste(Image.new("RGBA", (big, big), src) if isinstance(src, str) else src, (0, 0), m)
    stem_l = mask(lambda d: d.rectangle([STEM_X0 * k, TOP * k, STEM_MID * k, BOT * k], fill=255), soft=False)
    stem_r = mask(lambda d: d.rectangle([STEM_MID * k, TOP * k, STEM_X1 * k, BOT * k], fill=255), soft=False)
    paint(stem_l, brushed(big, k, *SILVER_L, STEM_X0, STEM_MID, 1))
    paint(stem_r, brushed(big, k, *SILVER_D, STEM_MID, STEM_X1, 2))
    lower_clip = mask(lambda d: d.rounded_rectangle([-big, -big, RIGHT * k, BOT * k], radius=END_R * k, fill=255), soft=False)
    upper_clip = mask(lambda d: d.rounded_rectangle([-big, TOP * k, RIGHT * k, 2 * big], radius=END_R * k, fill=255), soft=False)
    lower = Image.composite(mask(lambda d: d.polygon(S(LOWER), fill=255)), zero, lower_clip)
    paint(lower, DEEP)
    paint(Image.composite(lower, zero, mask(lambda d: d.polygon(S(SHADOW_BAND), fill=255), soft=False)), SHADOW)
    paint(Image.composite(mask(lambda d: d.polygon(S(UPPER), fill=255)), zero, upper_clip), BLUE)
    return img.resize((size, size), Image.LANCZOS)

def svg():
    h = lambda v: f"{v / 2:g}"
    p = lambda pts: "M" + "L".join(f"{h(x)} {h(y)}" for x, y in pts) + "Z"
    r = END_R
    upper = (f"M{h(UPPER[0][0])} {h(UPPER[0][1])}L{h(UPPER[1][0])} {h(UPPER[1][1])}"
             f"L{h(RIGHT - r)} {h(TOP)}A{h(r)} {h(r)} 0 0 1 {h(RIGHT)} {h(TOP + r)}"
             f"L{h(UPPER[3][0])} {h(UPPER[3][1])}L{h(UPPER[4][0])} {h(UPPER[4][1])}Z")
    lower = (f"M{h(LOWER[0][0])} {h(LOWER[0][1])}L{h(LOWER[1][0])} {h(LOWER[1][1])}"
             f"L{h(RIGHT)} {h(BOT - r)}A{h(r)} {h(r)} 0 0 1 {h(RIGHT - r)} {h(BOT)}"
             f"L{h(LOWER[3][0])} {h(LOWER[3][1])}L{h(LOWER[4][0])} {h(LOWER[4][1])}Z")
    e = A + SHADE
    shade = p([((A - LO_D2) / 2, (A + LO_D2) / 2), ((e - LO_D2) / 2, (e + LO_D2) / 2),
               ((e - LO_D1) / 2, (e + LO_D1) / 2), ((A - LO_D1) / 2, (A + LO_D1) / 2)])
    return ("<svg viewBox='0 0 512 512' width='100%' height='100%'><defs>"
            f"<linearGradient id='sl' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='{SILVER_L[0]}'/><stop offset='1' stop-color='{SILVER_L[1]}'/></linearGradient>"
            f"<linearGradient id='sd' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='{SILVER_D[0]}'/><stop offset='1' stop-color='{SILVER_D[1]}'/></linearGradient></defs>"
            f"<rect x='{h(STEM_X0)}' y='{h(TOP)}' width='{h(STEM_MID - STEM_X0)}' height='{h(BOT - TOP)}' fill='url(#sl)'/>"
            f"<rect x='{h(STEM_MID)}' y='{h(TOP)}' width='{h(STEM_X1 - STEM_MID)}' height='{h(BOT - TOP)}' fill='url(#sd)'/>"
            f"<path fill='{DEEP}' d='{lower}'/><path fill='{SHADOW}' d='{shade}'/><path fill='{BLUE}' d='{upper}'/></svg>")

def installer_images(out):
    """WiX installer art: white background with the mark where the old one sat."""
    dialog = Image.new("RGBA", (493, 312), (255, 255, 255, 255))
    dialog.alpha_composite(render(100), (30, 93))        # about 89x92, centered where the old 77x127 mark sat
    dialog.convert("RGB").save(os.path.join(out, "WixUI_Dialog.png"))
    banner = Image.new("RGBA", (493, 58), (255, 255, 255, 255))
    banner.alpha_composite(render(34), (450, 12))        # mark about 19x31 at (451, 13)
    banner.convert("RGB").save(os.path.join(out, "WixUI_Banner.png"))

def main(out):
    os.makedirs(out, exist_ok=True)
    cache = {}
    def R(s):
        if s not in cache: cache[s] = render(s)
        return cache[s]
    for s in (32, 128, 256, 512):
        R(s).save(os.path.join(out, f"logo-{s}.png"))
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [R(s) for s in sizes]
    frames[-1].save(os.path.join(out, "main.ico"), sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    open(os.path.join(out, "logo.svg"), "w").write(svg() + chr(10))
    installer_images(out)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python make-logo.py <output folder>")
    main(sys.argv[1])
