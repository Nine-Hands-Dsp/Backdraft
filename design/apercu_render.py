import math, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

S = 2                      # supersampling
W, H = 1360, 1400          # final size
w, h = W * S, H * S
F = "/usr/share/fonts/opentype/inter/Inter-%s.otf"
def font(style, size): return ImageFont.truetype(F % style, int(size * S))
def s(v): return int(round(v * S))

ACC = (255, 122, 38)       # braise
ACC2 = (255, 186, 72)
GREY = (122, 122, 128)
LIGHT = (214, 214, 218)

img = Image.new("RGBA", (w, h), (0, 0, 0, 0))

def rrect_mask(box, r):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([s(v) for v in box], s(r), fill=255)
    return m

def paste_color(mask, color, alpha=1.0):
    layer = Image.new("RGBA", (w, h), color + (0,))
    a = mask.point(lambda v: int(v * alpha))
    layer.putalpha(a)
    img.alpha_composite(layer)

def diag_light(base_rgb, box, g0=0.86, g1=1.10, sheen=12):
    """Facade light from top-left at 45 deg + satin band perpendicular."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    x0, y0, x1, y1 = [v * S for v in box]
    t = ((xx - x0) + (yy - y0)) / ((x1 - x0) + (y1 - y0))
    t = np.clip(t, 0, 1)
    gain = g1 + (g0 - g1) * t
    band = np.exp(-((t - 0.32) / 0.12) ** 2) * sheen
    arr = np.zeros((h, w, 4), np.float32)
    for i in range(3):
        arr[..., i] = base_rgb[i] * gain + band
    arr[..., 3] = 255
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")

def noise_layer(amount=5, seed=1):
    rng = np.random.default_rng(seed)
    n = rng.normal(0, amount, (h, w)).astype(np.float32)
    return n

# ---------------------------------------------------------------- top bar
top = rrect_mask((0, 0, W, H), 34)
paste_color(top, (20, 20, 22))

# ---------------------------------------------------------------- main body
BODY = (10, 110, W - 10, H - 10)
# drop shadow of body on bar
sh = rrect_mask((BODY[0] + 4, BODY[1] + 8, BODY[2] + 4, BODY[3] + 8), 38).filter(ImageFilter.GaussianBlur(s(10)))
paste_color(sh, (0, 0, 0), 0.6)
body_mask = rrect_mask(BODY, 38)
body = diag_light((38, 38, 41), BODY)
arr = np.array(body).astype(np.float32)
arr[..., :3] += noise_layer(2.2)[..., None]
body = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
body.putalpha(body_mask)
img.alpha_composite(body)
# rim: light top-left, dark bottom-right
rim = Image.new("L", (w, h), 0)
ImageDraw.Draw(rim).rounded_rectangle([s(v) for v in BODY], s(38), outline=255, width=s(2))
gy, gx = np.mgrid[0:h, 0:w].astype(np.float32)
tt = np.clip((gx + gy) / (w + h), 0, 1)
rim_hi = (np.array(rim) * (1 - tt) * 0.22).astype(np.uint8)
lay = Image.new("RGBA", (w, h), (255, 255, 255, 0)); lay.putalpha(Image.fromarray(rim_hi)); img.alpha_composite(lay)
rim_lo = (np.array(rim) * tt * 0.7).astype(np.uint8)
lay = Image.new("RGBA", (w, h), (0, 0, 0, 0)); lay.putalpha(Image.fromarray(rim_lo)); img.alpha_composite(lay)

# ---------------------------------------------------------------- display (inset screen) with knob notch
KX, KY = 680, 792
DISP = (30, 130, W - 30, 578)
disp_mask = rrect_mask(DISP, 24)
notch = Image.new("L", (w, h), 0)
ImageDraw.Draw(notch).ellipse([s(KX - 318), s(KY - 318), s(KX + 318), s(KY + 318)], fill=255)
notch = notch.filter(ImageFilter.GaussianBlur(s(1)))
disp_mask = ImageChops.subtract(disp_mask, notch)
# inner shadow look: dark screen
scr = Image.new("RGBA", (w, h), (17, 17, 19, 0)); scr.putalpha(disp_mask); img.alpha_composite(scr)
# inner shadow top-left (screen is recessed -> shadow on top/left edges)
inv = ImageChops.invert(disp_mask)
inner = inv.filter(ImageFilter.GaussianBlur(s(8)))
inner = ImageChops.offset(inner, s(3), s(4))
inner = ImageChops.multiply(inner, disp_mask)
lay = Image.new("RGBA", (w, h), (0, 0, 0, 0)); lay.putalpha(inner.point(lambda v: int(v * 0.85))); img.alpha_composite(lay)
# bottom-right lip highlight
edge = ImageChops.subtract(disp_mask.filter(ImageFilter.MaxFilter(5)), disp_mask)
edge = ImageChops.offset(edge, s(1), s(1))
ea = (np.array(edge).astype(np.float32) * tt * 0.35).astype(np.uint8)
lay = Image.new("RGBA", (w, h), (255, 255, 255, 0)); lay.putalpha(Image.fromarray(ea)); img.alpha_composite(lay)

d = ImageDraw.Draw(img)

# ---------------------------------------------------------------- riser waveform (ember)
WX0, WX1, WY = 370, 1030, 336
n = 92
glow = Image.new("RGBA", (w, h), ACC + (0,))
gd = ImageDraw.Draw(glow)
gd.ellipse([s(560), s(WY - 70), s(1080), s(WY + 70)], fill=ACC + (110,))
glow = glow.filter(ImageFilter.GaussianBlur(s(60)))
glow.putalpha(ImageChops.multiply(glow.getchannel("A"), disp_mask))
img.alpha_composite(glow)
for i in range(n):
    t = i / (n - 1)
    x = WX0 + t * (WX1 - WX0)
    amp = 3 + 72 * (t ** 2.4) * (0.82 + 0.18 * math.sin(i * 1.7) ** 2)
    # ember gradient: dark red -> orange -> yellow
    if t < 0.6:
        k = t / 0.6; c = (int(150 + 105 * k), int(40 + 82 * k), int(30 + 8 * k))
    else:
        k = (t - 0.6) / 0.4; c = (255, int(122 + 90 * k), int(38 + 80 * k))
    a = int(110 + 145 * t)
    wd = 2.2 if t < 0.5 else 2.8
    d.rounded_rectangle([s(x - wd / 2), s(WY - amp), s(x + wd / 2), s(WY + amp)], s(1), fill=c + (a,))
# end playhead line
d.line([s(WX1 + 14), s(WY - 88), s(WX1 + 14), s(WY + 88)], fill=(255, 255, 255, 40), width=s(1))

# ---------------------------------------------------------------- display texts
def text(xy, t, f, fill, anchor="la", spacing=0):
    if spacing:
        x, y = xy
        total = sum(d.textlength(c, font=f) for c in t) + spacing * S * (len(t) - 1)
        if anchor[0] == "m": x = x * S - total / 2
        elif anchor[0] == "r": x = x * S - total
        else: x = x * S
        for c in t:
            d.text((x, y * S), c, font=f, fill=fill, anchor="l" + anchor[1])
            x += d.textlength(c, font=f) + spacing * S
        return x / S
    d.text((s(xy[0]), s(xy[1])), t, font=f, fill=fill, anchor=anchor)

# title + flame-ish icon (three concentric arcs rising)
ix, iy = 72, 176
for k, r in enumerate([16, 11, 6]):
    d.arc([s(ix - r), s(iy - r), s(ix + r), s(iy + r)], 200, 520 - k * 30, fill=(205, 205, 210, 255), width=s(2.6))
x = text((98, 176), "BACK", font("Light", 30), (150, 150, 156), "lm", spacing=5)
text((x + 4, 176), "DRAFT", font("Bold", 30), (232, 232, 236), "lm", spacing=5)

# VOX / INST tab
d.rounded_rectangle([s(570), s(136), s(790), s(194)], s(10), fill=(12, 12, 14, 255))
text((640, 166), "VOX", font("Bold", 24), ACC, "mm", spacing=2)
text((722, 166), "INST", font("Medium", 24), GREY, "mm", spacing=2)

# RESET
f = font("Medium", 23)
text((1302, 176), "RESET", f, GREY, "rm", spacing=3)
cx, cy = 1188, 176
d.arc([s(cx - 10), s(cy - 10), s(cx + 10), s(cy + 10)], -40, 250, fill=GREY, width=s(2.4))
d.polygon([(s(cx - 13), s(cy - 9)), (s(cx - 3), s(cy - 11)), (s(cx - 9), s(cy - 1))], fill=GREY)

# LISTEN / LOOP (bottom-left)
fy = 524
d.polygon([(s(62), s(fy - 11)), (s(62), s(fy + 11)), (s(80), s(fy))], fill=GREY)
text((92, fy), "LISTEN", f, GREY, "lm", spacing=3)
lx = 220
d.arc([s(lx - 13), s(fy - 9), s(lx + 13), s(fy + 9)], 200, 330, fill=GREY, width=s(2.4))
d.arc([s(lx - 13), s(fy - 9), s(lx + 13), s(fy + 9)], 20, 150, fill=GREY, width=s(2.4))
ax, ay = lx + 11.3, fy - 4.5
d.polygon([(s(ax + 4), s(ay - 5)), (s(ax + 5), s(ay + 4)), (s(ax - 4), s(ay + 2))], fill=GREY)
bx, by = lx - 11.3, fy + 4.5
d.polygon([(s(bx - 4), s(by + 5)), (s(bx - 5), s(by - 4)), (s(bx + 4), s(by - 2))], fill=GREY)
text((242, fy), "LOOP", f, GREY, "lm", spacing=3)

# CAPTURE / IMPORT (bottom-right)
rx = 1306
x_imp = text((rx, fy), "IMPORT", f, GREY, "rm", spacing=3)
# compute positions manually
imp_w = sum(d.textlength(c, font=f) for c in "IMPORT") / S + 3 * 5
x0 = rx - imp_w - 50
cap_w = sum(d.textlength(c, font=f) for c in "CAPTURE") / S + 3 * 6
text((x0, fy), "CAPTURE", f, LIGHT, "rm", spacing=3)
dot_x = x0 - cap_w - 20
d.ellipse([s(dot_x - 8), s(fy - 8), s(dot_x + 8), s(fy + 8)], fill=(235, 58, 40, 255))
gl = Image.new("RGBA", (w, h), (235, 58, 40, 0))
ImageDraw.Draw(gl).ellipse([s(dot_x - 16), s(fy - 16), s(dot_x + 16), s(fy + 16)], fill=(235, 58, 40, 120))
img.alpha_composite(gl.filter(ImageFilter.GaussianBlur(s(7))))
# import icon (tray + down arrow) left of IMPORT
ia = rx - imp_w - 18
d.line([s(ia - 9), s(fy + 2), s(ia - 9), s(fy + 9), s(ia + 9), s(fy + 9), s(ia + 9), s(fy + 2)], fill=GREY, width=s(2.2), joint="curve")
d.line([s(ia), s(fy - 11), s(ia), s(fy + 3)], fill=GREY, width=s(2.2))
d.polygon([(s(ia - 6), s(fy - 2)), (s(ia + 6), s(fy - 2)), (s(ia), s(fy + 5))], fill=GREY)

# DRAG pill at right of waveform
px0, px1, py0, py1 = 1118, 1290, 300, 372
d.rounded_rectangle([s(px0), s(py0), s(px1), s(py1)], s(14), outline=(70, 70, 76, 255), width=s(1.5), fill=(24, 24, 27, 255))
for r_ in range(3):
    for c_ in range(2):
        gx_ = px0 + 28 + c_ * 10; gy_ = (py0 + py1) / 2 - 10 + r_ * 10
        d.ellipse([s(gx_ - 2.2), s(gy_ - 2.2), s(gx_ + 2.2), s(gy_ + 2.2)], fill=(150, 150, 156, 255))
text((px0 + 108, (py0 + py1) / 2), "DRAG", font("SemiBold", 23), LIGHT, "mm", spacing=3)

# ---------------------------------------------------------------- knob housing (rounded well)
well = Image.new("L", (w, h), 0)
ImageDraw.Draw(well).ellipse([s(KX - 318), s(KY - 318), s(KX + 318), s(KY + 318)], fill=255)
# subtle darker ring around knob area -> same as body, just a soft bevel
bev = ImageChops.subtract(well, well.filter(ImageFilter.MinFilter(5)))
ba = (np.array(bev).astype(np.float32) * (1 - tt) * 0.18).astype(np.uint8)
lay = Image.new("RGBA", (w, h), (255, 255, 255, 0)); lay.putalpha(Image.fromarray(ba)); img.alpha_composite(lay)

# scale dots (270 deg sweep)
R_DOT = 288
N_DOT = 25
for i in range(N_DOT):
    a = math.radians(-135 + 270 * i / (N_DOT - 1))
    x = KX + R_DOT * math.sin(a); y = KY - R_DOT * math.cos(a)
    big = i in (0, N_DOT - 1, (N_DOT - 1) // 2) or i % 4 == 0
    r = 3.3 if big else 2.1
    col = (225, 225, 230, 255) if big else (110, 110, 116, 255)
    d.ellipse([s(x - r), s(y - r), s(x + r), s(y + r)], fill=col)

# knob: shadow (offset bottom-right), skirt, cap, indicator
KR = 240
shadow = Image.new("L", (w, h), 0)
ImageDraw.Draw(shadow).ellipse([s(KX - KR + 14), s(KY - KR + 20), s(KX + KR + 14), s(KY + KR + 20)], fill=255)
shadow = shadow.filter(ImageFilter.GaussianBlur(s(22)))
lay = Image.new("RGBA", (w, h), (0, 0, 0, 0)); lay.putalpha(shadow.point(lambda v: int(v * 0.8))); img.alpha_composite(lay)

def radial_disc(cx, cy, r, c_hi, c_lo, hi_off=(-0.45, -0.45)):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dx = (xx - cx * S) / (r * S); dy = (yy - cy * S) / (r * S)
    dist = np.sqrt(dx ** 2 + dy ** 2)
    # light from top-left 45 deg
    t = np.clip(((dx - hi_off[0]) + (dy - hi_off[1])) / 2.9, 0, 1)
    out = np.zeros((h, w, 4), np.float32)
    for i in range(3):
        out[..., i] = c_hi[i] * (1 - t) + c_lo[i] * t
    out[..., 3] = np.clip((1 - dist) * r * S * 1.5, 0, 1) * 255
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")

# skirt ring (outer)
img.alpha_composite(radial_disc(KX, KY, KR, (78, 78, 82), (14, 14, 16)))
# cap (slightly smaller), gentle gradient
img.alpha_composite(radial_disc(KX, KY, KR - 10, (48, 48, 52), (26, 26, 29)))
# cap face subtle concentric
img.alpha_composite(radial_disc(KX, KY, KR - 16, (44, 44, 48), (31, 31, 34)))
# rim highlights: top-left bright arc, bottom-right dark arc
hl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
hd = ImageDraw.Draw(hl)
hd.arc([s(KX - KR + 2), s(KY - KR + 2), s(KX + KR - 2), s(KY + KR - 2)], 160, 290, fill=(255, 255, 255, 120), width=s(2.5))
hd.arc([s(KX - KR + 11), s(KY - KR + 11), s(KX + KR - 11), s(KY + KR - 11)], 170, 280, fill=(255, 255, 255, 45), width=s(2))
hd.arc([s(KX - KR + 11), s(KY - KR + 11), s(KX + KR - 11), s(KY + KR - 11)], 350, 100, fill=(0, 0, 0, 120), width=s(2))
img.alpha_composite(hl.filter(ImageFilter.GaussianBlur(s(1.2))))
# soft satin reflection on cap (fixed, top-left)
refl = Image.new("L", (w, h), 0)
ImageDraw.Draw(refl).ellipse([s(KX - 170), s(KY - 200), s(KX + 40), s(KY - 40)], fill=255)
refl = refl.filter(ImageFilter.GaussianBlur(s(55)))
capm = Image.new("L", (w, h), 0); ImageDraw.Draw(capm).ellipse([s(KX - KR + 16), s(KY - KR + 16), s(KX + KR - 16), s(KY + KR - 16)], fill=255)
refl = ImageChops.multiply(refl, capm)
lay = Image.new("RGBA", (w, h), (255, 255, 255, 0)); lay.putalpha(refl.point(lambda v: int(v * 0.10))); img.alpha_composite(lay)

# indicator (pointing up = 12 o'clock; value "4 BARS")
d.rounded_rectangle([s(KX - 3.5), s(KY - KR + 22), s(KX + 3.5), s(KY - KR + 82)], s(3.5), fill=(232, 232, 236, 255))

# knob label
lab_y = 1112
fl = font("SemiBold", 29); fv = font("Medium", 29)
tw1 = sum(d.textlength(c, font=fl) for c in "REVERSE") / S + 3 * 6
tw2 = sum(d.textlength(c, font=fv) for c in "4 BARS") / S + 3 * 5
x0 = KX - (tw1 + 14 + tw2) / 2
x = text((x0, lab_y), "REVERSE", fl, (226, 226, 230), "lm", spacing=3)
text((x + 14, lab_y), "4 BARS", fv, (120, 120, 126), "lm", spacing=3)

# ---------------------------------------------------------------- effects row
effects = [("LO-FI", False), ("SHIMMER", True), ("DRIVE", True), ("TREMOLO", True)]
fe = font("Bold", 23)
widths = [sum(d.textlength(c, font=fe) for c in t) / S + 3 * (len(t) - 1) + 30 for t, _ in effects]
gap = 46
total = sum(widths) + gap * (len(effects) - 1)
x = KX - total / 2
ey = 1300
for (t, on), wd in zip(effects, widths):
    col = ACC if on else GREY
    # power icon
    cx, cy = x + 11, ey
    if on:
        g = Image.new("RGBA", (w, h), ACC + (0,))
        gdr = ImageDraw.Draw(g)
        gdr.arc([s(cx - 11), s(cy - 11), s(cx + 11), s(cy + 11)], -60, 240, fill=ACC + (200,), width=s(5))
        ft = font("Bold", 23)
        tx = x + 30
        for c in t:
            gdr.text((s(tx), s(ey)), c, font=ft, fill=ACC + (255,), anchor="lm"); tx += gdr.textlength(c, font=ft) / S + 3
        ga = g.getchannel("A").filter(ImageFilter.GaussianBlur(s(8))).point(lambda v: min(255, int(v * 1.1))); gb = Image.new("RGBA", (w, h), ACC + (0,)); gb.putalpha(ga); img.alpha_composite(gb)
    d.arc([s(cx - 10), s(cy - 10), s(cx + 10), s(cy + 10)], -60, 240, fill=col, width=s(2.4))
    d.line([s(cx), s(cy - 14), s(cx), s(cy - 2)], fill=col, width=s(2.4))
    text((x + 30, ey), t, fe, col if not on else (255, 150, 70), "lm", spacing=3)
    x += wd + gap

# ---------------------------------------------------------------- speaker-grill dots in the 4 corners
def grill(cx, cy, flipx, flipy):
    pattern = [(0, 0), (1, 0), (2, 0), (0, 1), (0, 2)]
    for (i, j) in pattern:
        x = cx + (i * 34) * flipx; y = cy + (j * 34) * flipy
        r = 8
        # recessed hole: dark with light lower-right lip
        d.ellipse([s(x - r), s(y - r), s(x + r), s(y + r)], fill=(22, 22, 24, 255))
        d.arc([s(x - r), s(y - r), s(x + r), s(y + r)], 300, 120, fill=(80, 80, 86, 255), width=s(1.5))
        d.arc([s(x - r + 1), s(y - r + 1), s(x + r - 1), s(y + r - 1)], 150, 280, fill=(5, 5, 6, 255), width=s(2))
grill(36, 610, 1, 1)
grill(W - 36, 610, -1, 1)
grill(36, H - 46, 1, -1)
grill(W - 36, H - 46, -1, -1)

# ---------------------------------------------------------------- top bar content
# logo
subprocess.run(["rsvg-convert", "-w", str(s(210)), "/home/claude/download/logo/NineHands_LogoPP_v003_FullWhite.svg", "-o", "/tmp/logo_w.png"], check=True)
logo = Image.open("/tmp/logo_w.png").convert("RGBA")
la = logo.getchannel("A").point(lambda v: int(v * 0.82)); logo.putalpha(la)
img.alpha_composite(logo, (s(34), s(55) - logo.height // 2))

# preset box
B0, B1, BY0, BY1 = 400, 900, 20, 90
d.rounded_rectangle([s(B0), s(BY0), s(B1), s(BY1)], s(10), outline=(78, 78, 84, 255), width=s(1.6), fill=(14, 14, 16, 255))
d.line([s(B1 - 148), s(BY0), s(B1 - 148), s(BY1)], fill=(78, 78, 84, 255), width=s(1.6))
d.line([s(B1 - 74), s(BY0), s(B1 - 74), s(BY1)], fill=(78, 78, 84, 255), width=s(1.6))
text((B0 + 24, 55), "Ignition", font("Medium", 26), (222, 222, 226), "lm")
for cx, dirn in ((B1 - 111, -1), (B1 - 37, 1)):
    d.line([s(cx + 5 * dirn * -1), s(45), s(cx + 5 * dirn), s(55), s(cx + 5 * dirn * -1), s(65)], fill=(222, 222, 226, 255), width=s(2.4), joint="curve")
# save button
SV0 = 922
d.rounded_rectangle([s(SV0), s(BY0), s(SV0 + 76), s(BY1)], s(10), outline=(78, 78, 84, 255), width=s(1.6), fill=(14, 14, 16, 255))
cx, cy = SV0 + 38, 55
d.rounded_rectangle([s(cx - 13), s(cy - 13), s(cx + 13), s(cy + 13)], s(3), outline=(222, 222, 226, 255), width=s(2.2))
d.rectangle([s(cx - 7), s(cy - 13), s(cx + 5), s(cy - 5)], outline=(222, 222, 226, 255), width=s(2))
d.line([s(cx + 8), s(cy + 5), s(cx + 8), s(cy + 17)], fill=(222, 222, 226, 255), width=s(2.2))
d.line([s(cx + 2), s(cy + 11), s(cx + 14), s(cy + 11)], fill=(222, 222, 226, 255), width=s(2.2))
# hamburger
for k in range(3):
    yk = 43 + k * 12
    d.line([s(W - 76), s(yk), s(W - 42), s(yk)], fill=(200, 200, 204, 255), width=s(2.4))

out = img.resize((W, H), Image.LANCZOS)
out.save("/home/claude/preview/backdraft_apercu.png")
print("ok")
