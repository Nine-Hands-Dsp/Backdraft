"""Calques du grand potard de Backdraft (nh::Knob) : PNG 1024 x 1024, fond transparent, même cadrage.
Lumière douce venant du haut à gauche, à 45° : seuls les reflets (calque highlight) portent la lumière,
les calques qui tournent sont neutres (couleur de la matière seule).
Usage : python3 tools/make_knob.py  -> assets/knob_*.png
"""
import math, os
import numpy as np
from PIL import Image, ImageFilter

N = 1024
SS = 2                      # suréchantillonnage
M = N * SS
C = M / 2
R_OUT = 0.80 * C            # rayon de la jupe (le composant laisse de la place à l'ombre)
R_CAP = R_OUT * 230 / 240
R_FACE = R_OUT * 224 / 240
OUT = os.path.join(os.path.dirname(__file__), "..", "assets")

yy, xx = np.mgrid[0:M, 0:M].astype(np.float32)
dx, dy = xx + 0.5 - C, yy + 0.5 - C
r = np.sqrt(dx * dx + dy * dy)
ang = np.arctan2(dy, dx)

def cover(rad, soft=1.2 * SS):
    return np.clip((rad - r) / soft + 0.5, 0, 1)

def save(name, rgb, a):
    arr = np.zeros((M, M, 4), np.float32)
    for i in range(3):
        arr[..., i] = rgb[i] if np.isscalar(rgb[i]) else rgb[i]
    arr[..., 3] = a * 255
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    # Pillow redimensionne les images RGBA en alpha prémultiplié (aucun liseré)
    im.resize((N, N), Image.LANCZOS).save(os.path.join(OUT, name))

# ---------------------------------------------------------------- ombre portée (centrée, sans décalage)
sh = Image.fromarray((cover(R_OUT * 0.99) * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(26 * SS))
save("knob_shadow.png", (0, 0, 0), np.array(sh).astype(np.float32) / 255 * 0.85)

# ---------------------------------------------------------------- jupe (couronne) : anneau satiné, fines stries radiales
ring = cover(R_OUT) * (1 - cover(R_CAP - 1.5 * SS))
stri = 0.5 + 0.5 * np.sin(ang * 180)                        # 180 stries très discrètes : rendent le flou visible
base = 44 + 6 * stri
save("knob_crown.png", (base, base, base + 3), ring)

# ---------------------------------------------------------------- chapeau : noir satiné, brossage concentrique (invariant)
rng = np.random.default_rng(7)
rad_noise = np.interp(r, np.arange(0, C + 2), rng.normal(0, 1, int(C) + 2)).astype(np.float32)
capv = 36 + 1.6 * rad_noise
edge = cover(R_CAP) * (1 - cover(R_FACE))                    # biseau entre chapeau et face
capv = capv - 7 * edge
# petite rainure sombre entre jupe et chapeau
groove = np.exp(-((r - R_CAP) / (1.6 * SS)) ** 2)
capv = capv - 10 * groove
save("knob_cap.png", (capv, capv, capv + 3), cover(R_CAP + 1.0 * SS))

# ---------------------------------------------------------------- index : trait clair pointé vers le haut
mw, top, bot = 6.0 * SS * 1.0, R_OUT - 0.093 * R_OUT * 2.2, R_OUT - 0.093 * R_OUT * 2.2 - 0.26 * R_OUT
mw = 0.030 * R_OUT
top = -(R_FACE - 0.06 * R_OUT)        # y (vers le haut négatif)
bot = top + 0.25 * R_OUT
# capsule verticale centrée en x = 0
cy = np.clip(dy, top + mw, bot - mw)
dist = np.sqrt(dx * dx + (dy - cy) ** 2)
mark = np.clip((mw - dist) / (1.0 * SS) + 0.5, 0, 1)
save("knob_marker.png", (234, 234, 238), mark)

# ---------------------------------------------------------------- reflets fixes : lumière en haut à gauche, à 45°
# t = 0 en haut à gauche, 1 en bas à droite (diagonale exacte)
L = np.array([-1.0, -1.0]) / math.sqrt(2)
proj = (dx * L[0] + dy * L[1]) / R_OUT                       # +1 vers la lumière
inside = cover(R_OUT)
capin = cover(R_CAP - 1 * SS)
# éclairage global : clair vers la lumière, sombre à l'opposé
lightA = np.clip(proj, 0, 1) ** 1.3 * 0.10 * capin
darkA = np.clip(-proj, 0, 1) ** 1.2 * 0.42 * inside
# jupe : face éclairée en haut à gauche, ombrée en bas à droite (cylindre)
skirt = inside * (1 - capin)
lightA += skirt * np.clip(proj, 0, 1) ** 0.8 * 0.24
darkA += skirt * np.clip(-proj, 0, 1) ** 0.8 * 0.35
# liseré de lumière sur l'arête extérieure, centré sur la diagonale haut-gauche
dang = np.angle(np.exp(1j * (ang - math.radians(-135))))
arc = np.exp(-(dang / 0.85) ** 2)
rim = np.exp(-((r - (R_OUT - 2.2 * SS)) / (1.8 * SS)) ** 2)
lightA += rim * arc * 0.32
rim2 = np.exp(-((r - (R_CAP - 2.0 * SS)) / (1.6 * SS)) ** 2)
lightA += rim2 * arc * 0.22
# arête basse-droite sombre
arcD = np.exp(-((np.angle(np.exp(1j * (ang - math.radians(45))))) / 0.9) ** 2)
darkA += rim2 * arcD * 0.35
# reflet satiné doux sur le chapeau, en haut à gauche
bx, by = -0.30 * R_OUT, -0.30 * R_OUT
blob = np.exp(-(((dx - bx) ** 2 + (dy - by) ** 2) / (0.42 * R_OUT) ** 2)) * capin
lightA += blob * 0.07
# composition : blanc (lumière) + noir (ombre) dans un seul calque
shade = lightA - darkA                                       # > 0 : lumière (blanc), < 0 : ombre (noir)
shade += rng.normal(0, 0.6 / 255, shade.shape) * inside      # tramage : pas de paliers visibles
alpha = np.clip(np.abs(shade), 0, 1)
white = np.where(shade > 0, 255.0, 0.0)
save("knob_highlight.png", (white, white, white), alpha)
print("calques écrits dans", os.path.abspath(OUT))
