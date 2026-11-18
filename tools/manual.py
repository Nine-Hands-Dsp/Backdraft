# BACKDRAFT (NINE HANDS | DSP) - notice PDF en français, illustrée avec le rendu réel du plugin (tools/render).
# Usage : python3 tools/manual.py <dossier des captures (sortie de tools/render)> <fichier PDF de sortie>
import os, sys, re, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1]
OUT = sys.argv[2]
VERSION = re.search(r"project\(Backdraft VERSION ([0-9.]+)\)", open(os.path.join(ROOT, "CMakeLists.txt")).read()).group(1)
TMP = os.path.join(os.path.dirname(os.path.abspath(OUT)), "notice_tmp")
os.makedirs(TMP, exist_ok=True)

FONTS = os.path.join(ROOT, "fonts"); FONTS2 = os.path.join(ROOT, "tools", "fonts_notice")
for n, f, d in (("Pop", "Poppins-Regular", FONTS), ("PopL", "Poppins-Light", FONTS), ("PopM", "Poppins-Medium", FONTS), ("PopB", "Poppins-Bold", FONTS),
                ("Title", "BarlowCondensed-ExtraBold", FONTS2)):
    pdfmetrics.registerFont(TTFont(n, os.path.join(d, f + ".ttf")))

ACC = colors.HexColor("#E8661A"); ACC_L = colors.HexColor("#FFBA48")        # braise de Backdraft
DARK = colors.HexColor("#1C1A1A"); GREY = colors.HexColor("#6B6662"); LIGHT = colors.HexColor("#FBEFE6")
LOGO_PNG = os.path.join(TMP, "nine_hands_logo_blanc.png")
subprocess.run(["rsvg-convert", "-w", "1200", os.path.join(ROOT, "logo", "NineHandsLogoWhite.svg"), "-o", LOGO_PNG], check=True)

def shot(name): return Image.open(os.path.join(SHOTS, name)).convert("RGB")
src = shot("ui_75.png")
k = src.width / 680.0                                  # repère de l'interface : 680 x 700 unités

# ---------- capture annotée ----------
marks = [(1, 312, 27), (2, 480, 27), (3, 650, 27), (4, 340, 112), (5, 600, 112), (6, 240, 190), (7, 602, 205),
         (8, 66, 238), (9, 132, 238), (10, 540, 238), (11, 624, 238), (12, 236, 470), (13, 70, 650)]
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(FONTS, "Poppins-Bold.ttf"), int(15 * k))
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 12 * k
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(232, 102, 26), width=max(2, int(3 * k)))
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(140, 50, 0), anchor="mm")
annotated = os.path.join(TMP, "annotated.png"); ann.save(annotated)

def crop_u(im, box): return im.crop(tuple(int(v * k) for v in box))
disp_rec = os.path.join(TMP, "rec.png"); crop_u(shot("ui_capture.png"), (15, 65, 665, 289)).save(disp_rec)
disp_empty = os.path.join(TMP, "empty.png")
e = shot("ui_empty.png"); ke = e.width / 680.0
e.crop(tuple(int(v * ke) for v in (15, 65, 665, 289))).resize((int(650 * k), int(224 * k))).save(disp_empty)
disp_listen = os.path.join(TMP, "listen.png"); crop_u(shot("ui_fx_listen.png"), (15, 65, 665, 289)).save(disp_listen)
fxrow = os.path.join(TMP, "fx.png"); crop_u(shot("ui_fx_listen.png"), (60, 630, 620, 670)).save(fxrow)
bars = os.path.join(TMP, "bars.png")
a = crop_u(src, (195, 6, 505, 49)); b = crop_u(shot("ui_fx_listen.png"), (195, 6, 505, 49))
st_img = Image.new("RGB", (a.width, a.height * 2 + 12), (255, 255, 255)); st_img.paste(a, (0, 0)); st_img.paste(b, (0, a.height + 12)); st_img.save(bars)

# ---------- couverture : fenêtre détourée (coins arrondis) sur fond noir, reflet au sol, halo braise très doux ----------
cutw, cuth = src.size
rmask = Image.new("L", src.size, 0); ImageDraw.Draw(rmask).rounded_rectangle((0, 0, cutw - 1, cuth - 1), radius=int(14 * k), fill=255)
cut = Image.new("RGBA", src.size, (0, 0, 0, 0)); cut.paste(src, (0, 0), rmask)
cw, ch = cut.size
pad = int(cw * 0.10)
canvas = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.30) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.45
glow = np.exp(-(((xx - gcx) / (cw * 0.52)) ** 2 + ((yy - gcy) / (ch * 0.48)) ** 2)) * 34
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
halo = np.dstack([glow * 1.0, glow * 0.48, glow * 0.15, np.full_like(glow, 255)]).astype(np.uint8)
canvas = Image.alpha_composite(canvas, Image.fromarray(halo, "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM)
ra = np.array(refl).astype(np.float64)
fade = np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22
ra[..., 3] *= fade[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
canvas = canvas.crop((0, 0, canvas.width, pad + int(ch * 1.20)))
cover = os.path.join(TMP, "cover_cutout.png"); canvas.convert("RGB").save(cover)
COVER_PAD, COVER_CW, COVER_CH = pad, cw, ch

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

# ---------- styles ----------
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY, spaceAfter=6),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items): return [P("&bull;&nbsp;&nbsp;" + i) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#E5DAD2")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t
def side(text_flow, image_path, caption, iw=78 * mm):
    t = Table([[text_flow, [img(image_path, iw), P(caption, "small")]]], colWidths=[W - iw - 4 * mm, iw + 4 * mm])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
    return t

W = A4[0] - 36 * mm
f = [NextPageTemplate("p"), Spacer(1, 1), PageBreak()]

# ---------- 1. Présentation ----------
f.append(P("1. Présentation", "h1"))
f += [P("<b>Backdraft</b> fabrique en un clic une <b>montée de reverb inversée</b> (« reverse reverb », « riser ») à partir d'une phrase : une voix, une guitare, un synthé. Il s'inspire d'<b>AtmosRiser</b> de WaveAtmos : plus besoin d'exporter la phrase, la retourner, ajouter une reverb, exporter à nouveau et retourner le résultat. Backdraft fait tout ce travail lui-même et vous donne le riser fini, à glisser dans la timeline."),
      P("Le nom vient du « backdraft » des pompiers : l'appel d'air qui aspire avant l'embrasement. C'est exactement l'effet d'un riser : un souffle qui monte et qui s'arrête pile au moment où la phrase démarre.")]
f.append(P("Ce que Backdraft sait faire", "h2"))
f += bullets(["Capturer ce qui passe sur la piste (<b>CAPTURE</b>) ou charger un fichier audio (<b>IMPORT</b>, ou glisser-déposer sur l'écran).",
              "Calculer le riser : longueur calée sur le tempo du projet, de 1 temps à 8 mesures (potard <b>REVERSE</b>).",
              "Mode <b>VOX</b> avec anti-sifflantes automatique, ou <b>INST</b> pour les instruments.",
              "Six effets en un clic : PHONE, LO-FI, CHORUS, SHIMMER, DRIVE, TREMOLO.",
              "Écouter le résultat (<b>LISTEN</b>, <b>LOOP</b>) puis le glisser dans Reaper (<b>DRAG</b>).",
              "13 presets d'usine, vos propres presets, MIDI Learn sur tous les réglages."])

# ---------- 2. Installation ----------
f.append(P("2. Installation", "h1"))
f.append(P("Windows", "h2"))
f.append(P("Téléchargez <b>Backdraft-VST3-Windows.zip</b>, décompressez-le et copiez le dossier <b>Backdraft.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3</b>."))
f.append(P("macOS", "h2"))
f.append(P("Téléchargez <b>Backdraft-VST3-macOS.zip</b>, décompressez-le et copiez <b>Backdraft.vst3</b> dans <b>~/Library/Audio/Plug-Ins/VST3</b> (binaire universel Apple Silicon et Intel). Le plugin est signé ad hoc : au premier lancement, autorisez-le si besoin dans Réglages Système &gt; Confidentialité et sécurité."))
f.append(P("Dans Reaper", "h2"))
f.append(P("Options &gt; Preferences &gt; Plug-ins &gt; VST : <b>Re-scan</b>. Backdraft apparaît sous le nom <b>Backdraft (NINE HANDS | DSP)</b>. Placez-le en insert sur la piste de la phrase (voix, guitare…) : le son de la piste le traverse <b>sans aucune modification</b>, il ne fait qu'écouter et calculer."))
f.append(PageBreak())

# ---------- 3. Vue d'ensemble ----------
f.append(P("3. Vue d'ensemble", "h1"))
f.append(img(annotated, W * 0.86)); f.append(Spacer(1, 3 * mm))
ov = [["N°", "Élément", "Rôle"],
      ["1", "Barre de presets", "Flèches : preset précédent / suivant ; clic sur le nom : liste des presets. Devient la ligne d'état pendant un réglage."],
      ["2", "Sauvegarde", "Enregistre les réglages comme preset (.backdraft)."],
      ["3", "Menu", "Presets, import, dossiers, MIDI, taille de la fenêtre, Glow (aussi par clic droit sur la façade)."],
      ["4", "VOX / INST", "Préparation de la phrase pour une voix ou un instrument (section 5)."],
      ["5", "RESET", "Revient au preset Ignition (réglages par défaut)."],
      ["6", "Écran", "Forme de la montée calculée ; la ligne verticale à droite marque le début de la phrase."],
      ["7", "DRAG", "Glisser le riser vers la timeline du DAW."],
      ["8, 9", "LISTEN, LOOP", "Écoute de la montée puis de la phrase ; LOOP la répète."],
      ["10, 11", "CAPTURE, IMPORT", "Source du riser : la piste en direct ou un fichier audio."],
      ["12", "REVERSE", "Longueur de la montée, en temps et mesures du projet."],
      ["13", "Effets", "PHONE, LO-FI, CHORUS, SHIMMER, DRIVE, TREMOLO, cumulables."]]
f.append(table(ov, [12 * mm, 34 * mm, W - 46 * mm]))
f.append(PageBreak())

# ---------- 4. Source ----------
f.append(P("4. Donner une phrase à Backdraft", "h1"))
f.append(side([P("CAPTURE : la piste en direct", "h2"),
               P("Lancez la lecture un peu avant la phrase, cliquez <b>CAPTURE</b> : le point rouge devient un carré, l'écran affiche la prise en rouge avec sa durée (REC). Cliquez <b>STOP</b> après la phrase (ou au moins après ses premiers mots). La prise s'arrête d'elle-même au bout de 30 s."),
               P("Le silence au début et à la fin est retiré automatiquement : seul le début réel de la phrase compte. Chaque prise est enregistrée en WAV dans <b>Documents/Nine Hands DSP/Backdraft/Captures</b> et rechargée avec le projet.")],
              disp_rec, "Capture en cours."))
f.append(side([P("IMPORT : un fichier", "h2"),
               P("Cliquez <b>IMPORT</b> (ou menu &gt; Import audio file...) et choisissez un fichier WAV, AIFF, FLAC, MP3 ou OGG. Vous pouvez aussi <b>glisser un fichier sur la fenêtre</b> depuis l'explorateur ou depuis Reaper. Le fichier est converti au taux d'échantillonnage du projet ; les 30 premières secondes sont gardées.")],
              disp_empty, "Sans phrase, l'écran invite à capturer ou importer."))
f.append(P("Le riser est recalculé tout seul après chaque changement (phrase, réglage, tempo du projet) ; pendant le calcul, la forme de la montée s'assombrit un instant.", "small"))

# ---------- 5. Réglages ----------
f.append(P("5. Les réglages", "h1"))
f.append(P("REVERSE", "h2"))
f.append(P("Le grand potard choisit la longueur de la montée : <b>1 BEAT, 2 BEATS, 1 BAR, 2 BARS, 4 BARS, 8 BARS</b> (une mesure = 4 temps). La durée suit le tempo du projet : 4 BARS à 120 BPM = 8 secondes. Glisser verticalement (Maj : réglage fin), molette (un cran par mouvement), double-clic : 4 BARS. La reverb est réglée d'après cette longueur pour que sa queue garde du corps sur toute la montée."))
f.append(P("VOX / INST", "h2"))
f += bullets(["<b>VOX</b> : coupe le grave sous 170 Hz, légère présence, et <b>anti-sifflantes automatique</b> : Backdraft repère les « S », « CH », « T » (énergie au-dessus de 4,8 kHz) et ne baisse les aigus que pendant ces sons, pour qu'ils ne sifflent pas à l'envers dans la montée. Le reste de la phrase garde tout son éclat.",
              "<b>INST</b> : pour une guitare, un piano, un synthé : coupe seulement sous 60 Hz, pas d'anti-sifflantes, reverb un peu plus claire et plus courte."])
f.append(P("Effets en un clic", "h2"))
f.append(img(fxrow, W * 0.8))
f.append(table([["Effet", "Ce qu'il fait"],
                ["PHONE", "Bande étroite 420 Hz - 3,1 kHz avec bosse à 1,5 kHz et légère saturation : riser « téléphone » ou radio."],
                ["LO-FI", "Échantillonnage réduit à 11 kHz, 9 bits, filtre 5,2 kHz : grain de vieux sampler."],
                ["CHORUS", "Deux voix modulées croisées gauche / droite : montée plus large et plus mouvante."],
                ["SHIMMER", "Une copie à l'octave supérieure est envoyée dans la reverb : montée scintillante, très aérienne."],
                ["DRIVE", "Saturation qui augmente avec la montée : douce au début, rugueuse à l'arrivée."],
                ["TREMOLO", "Pulsation qui accélère de 3 à 15 Hz et se creuse en approchant de la phrase : tension maximale avant le drop."]],
               [26 * mm, W - 26 * mm]))
f.append(P("Les effets se cumulent (ordre : PHONE, LO-FI, CHORUS, DRIVE, TREMOLO ; SHIMMER agit dans la reverb). La montée est toujours normalisée à −1 dBFS crête.", "small"))

# ---------- 6. Écoute et glisser ----------
f.append(P("6. Écouter et poser le riser", "h1"))
f.append(side([P("<b>LISTEN</b> joue la montée puis le début de la phrase d'origine (4 s), mélangés au son de la piste, dans la sortie du plugin ; un second clic l'arrête. Un trait blanc suit la lecture et la ligne de fin s'allume quand la phrase arrive. <b>LOOP</b> répète l'écoute."),
               P("<b>DRAG</b> : cliquez sur la pastille DRAG (ou sur la forme d'onde) et faites glisser vers la timeline de Reaper. Le riser est écrit en WAV 24 bits dans <b>Documents/Nine Hands DSP/Backdraft/Risers</b> (nom : preset, longueur, tempo, heure)."),
               P("<b>Placement</b> : la montée finit exactement là où la phrase commence. Posez donc le fichier de façon que sa <b>fin</b> tombe sur le début de la phrase (dans Reaper, alignez le bord droit de l'objet sur l'attaque, ou placez son début une durée REVERSE plus tôt).")],
              disp_listen, "Écoute en cours, effets CHORUS, SHIMMER, DRIVE et TREMOLO allumés."))

# ---------- 7. MIDI ----------
f.append(P("7. MIDI et automation", "h1"))
f += bullets(["Tous les réglages sont automatisables dans le DAW : Reverse length, Mode, Phone, Lo-fi, Chorus, Shimmer, Drive, Tremolo.",
              "<b>MIDI Learn</b> : clic droit sur le potard REVERSE, sur VOX / INST ou sur un effet &gt; <b>MIDI Learn</b>, puis bougez le bouton ou la pédale MIDI. La ligne d'état affiche MIDI LEARN puis le CC appris. <b>Clear MIDI</b> retire l'assignation ; menu &gt; MIDI &gt; <b>Clear all MIDI</b> les retire toutes.",
              "Aucun CC n'est assigné par défaut. Les assignations sont sauvegardées avec le projet. Une pédale d'expression est apprise sur toute sa course réelle."])

# ---------- 8. Fenêtre ----------
f.append(P("8. Fenêtre et ligne d'état", "h1"))
wt = Table([[[P("La fenêtre s'ouvre à la taille par défaut (<b>60 %</b>) et ne peut pas être réduite en dessous. Menu ou clic droit &gt; <b>Plugin size</b> : 60 %, 75 % ou 100 % (taille enregistrée avec le projet) ; le coin en bas à droite agrandit jusqu'à 150 % en gardant les proportions. <b>Glow intensity</b> : 25, 50, 75 ou 100 % pour l'intensité des halos (montée, effets, point de capture)."),
              P("La <b>barre de presets</b> et la <b>ligne d'état</b> partagent le même cadre. Dès qu'un réglage bouge (souris, molette, automation, MIDI), la ligne d'état prend sa place en orange (longueur et tempo, mode, effet allumé ou éteint, capture…) ; elle s'efface 2,5 s après. Un clic sur le cadre fait revenir la barre de presets tout de suite.")],
             [img(bars, 66 * mm), P("Au repos (barre de presets) et après un réglage (ligne d'état).", "small")]]], colWidths=[W - 70 * mm, 70 * mm])
wt.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
f.append(wt)

# ---------- 9. Presets ----------
f.append(P("9. Presets", "h1"))
f.append(P("Flèches de la barre de presets, clic sur son nom, menu &gt; Presets, ou menu de presets du DAW. Un astérisque après le nom signale un preset modifié. Le bouton de sauvegarde et <b>Save preset...</b> enregistrent vos réglages (fichiers .backdraft, dossier Documents/Nine Hands DSP/Backdraft/Presets) ; ils apparaissent ensuite dans la liste."))
f.append(table([["Preset", "Longueur", "Mode", "Effets", "Caractère"],
                ["Ignition", "4 bars", "VOX", "aucun", "Le riser de référence, propre et ample."],
                ["Slow Burn", "8 bars", "VOX", "CHORUS", "Très longue montée large, pour une intro."],
                ["Afterglow", "4 bars", "VOX", "CHORUS, SHIMMER", "Montée lumineuse et scintillante."],
                ["Smoke Signal", "2 bars", "VOX", "PHONE", "Riser téléphone, court et serré."],
                ["Ember Tape", "4 bars", "VOX", "LO-FI", "Grain de vieux sampler."],
                ["Flashover", "2 bars", "VOX", "DRIVE, TREMOLO", "Tension saturée qui accélère avant le drop."],
                ["Ghost Call", "4 bars", "VOX", "PHONE, TREMOLO", "Voix lointaine qui pulse."],
                ["Halo", "8 bars", "VOX", "SHIMMER", "Nappe aérienne à l'octave."],
                ["Wildfire", "4 bars", "VOX", "CHORUS, SHIMMER, DRIVE, TREMOLO", "Tout en même temps : riser d'EDM."],
                ["Quick Spark", "2 beats", "VOX", "aucun", "Aspiration très courte avant un mot."],
                ["Inst Swell", "4 bars", "INST", "aucun", "Montée naturelle d'un instrument."],
                ["Inst Cathedral", "8 bars", "INST", "CHORUS, SHIMMER", "Grand espace pour guitare ou piano."],
                ["Inst Furnace", "2 bars", "INST", "DRIVE", "Montée saturée pour riff de guitare."]],
               [25 * mm, 20 * mm, 12 * mm, 46 * mm, W - 103 * mm]))

# ---------- 10. Conseils ----------
f.append(P("10. Conseils d'utilisation", "h1"))
f += bullets(["Pour une voix, capturez au moins le premier mot complet : c'est son attaque qui nourrit la montée.",
              "Pour une guitare (7 cordes en Sol ou plus bas comprise), passez en <b>INST</b> : le grave est gardé, la montée garde le poids de l'accord. Placez Backdraft sur la piste de la guitare déjà amplifiée (après Ampire et Impulse) pour que le riser ait le son de l'ampli.",
              "Une phrase sèche donne le riser le plus net ; une phrase déjà réverbérée donne une montée plus floue.",
              "Si la montée paraît trop courte ou trop longue, changez REVERSE : la reverb est réajustée à chaque longueur.",
              "Mettez le riser sur sa propre piste pour le régler (volume, filtre, panoramique) indépendamment de la phrase."])

# ---------- 11. Caractéristiques ----------
f.append(KeepTogether([P("11. Caractéristiques techniques", "h1"), table([
    ["Format", "VST3 (Windows 64 bits, macOS universel), mono ou stéréo ; le signal de la piste passe sans modification, aucune latence"],
    ["Source", "capture de la piste jusqu'à 30 s, ou fichier WAV / AIFF / FLAC / MP3 / OGG (30 s), converti au taux du projet"],
    ["Montée", "1 temps à 8 mesures selon le tempo du projet ; attaque de la phrase (0,35 à 1,6 s) retournée dans une reverb FDN 8 lignes (matrice de Householder, diffusion d'entrée, amortissement des aigus), queue retournée"],
    ["Reverb", "RT60 = 2 x la longueur + 1,2 s (2,5 à 40 s)"],
    ["Anti-sifflantes", "VOX : détection 4,8 kHz, atténuation des aigus pendant les sifflantes seulement"],
    ["Sortie", "riser normalisé à −1 dBFS crête, WAV 24 bits stéréo"],
    ["MIDI", "MIDI Learn sur tous les réglages, aucun CC par défaut"],
    ["Fenêtre", "60 / 75 / 100 %, jusqu'à 150 %, jamais sous 60 % ; anti-gel v2 sous Windows"]],
    [28 * mm, W - 28 * mm], header=False)]))

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "Backdraft (NINE HANDS | DSP) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

# ---- Couverture « minimale » (modèle 6) : gabarit fixe commun à toutes les notices ----
COVER_NAME = "BACKDRAFT"; COVER_SUB = "Riser reverse reverb"
PEDAL_H, PEDAL_MAXW, PEDAL_TOP = 82 * mm, 115 * mm, 84 * mm
LANG_TOP = 18 * mm
NAME_TOP, NAME_PT = 194 * mm, 30
SUB_TOP, SUB_PT = 208 * mm, 10
LOGO_W, LOGO_BOTTOM = 38 * mm, 22 * mm
BAND_H = 3 * mm
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, 0, A4[0], BAND_H, stroke=0, fill=1)
    def line(text, top, font, size, cs, col):
        w = pdfmetrics.stringWidth(text, font, size) + cs * (len(text) - 1)
        t = c.beginText((A4[0] - w) / 2, A4[1] - top - size * 0.8); t.setFont(font, size); t.setCharSpace(cs)
        t.setFillColor(col); t.textOut(text); c.drawText(t)
    line("FRANÇAIS", LANG_TOP, "PopM", 8.5, 0, ACC_L)
    ci = Image.open(cover); iw, ih = ci.size
    kk = min(PEDAL_H / COVER_CH, PEDAL_MAXW / COVER_CW)
    dw, dh = iw * kk, ih * kk
    y0 = A4[1] - (PEDAL_TOP + (PEDAL_H - COVER_CH * kk) / 2) + COVER_PAD * kk
    c.drawImage(cover, (A4[0] - dw) / 2, y0 - dh, dw, dh)
    line(COVER_NAME, NAME_TOP - NAME_PT * 0.02, "Title", NAME_PT, NAME_PT * 0.5, colors.white)
    line(COVER_SUB, SUB_TOP, "Pop", SUB_PT, 1.5, colors.HexColor("#BDC4B8"))
    lw, lh = Image.open(LOGO_PNG).size
    c.drawImage(LOGO_PNG, (A4[0] - LOGO_W) / 2, LOGO_BOTTOM, LOGO_W, LOGO_W * lh / lw, mask="auto")
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="BACKDRAFT - Notice v" + VERSION, author="NINE HANDS | DSP")
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
doc.build(f)
print(OUT)
