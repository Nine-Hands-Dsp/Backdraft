#include "PluginEditor.h"
#include "NineHandsDefaultSize.h"
#include "NineHandsStatusText.h"
#include "BinaryData.h"

using namespace juce;
using namespace BackdraftUi;
using namespace backdraft;

static double nowS() { return Time::getMillisecondCounterHiRes() * 0.001; }

namespace
{
    // repères de la façade (unités du repère 680 x 700)
    const Point<float> kKnobC { 340.0f, 396.0f };
    constexpr float kKnobBox = 300.0f;          // côté du composant potard
    constexpr float kNotchR = 159.0f;           // échancrure de l'écran autour du potard
    constexpr float kScaleR = 144.0f;           // graduations
    const Rectangle<float> kBody    { 5.0f, 55.0f, 670.0f, 640.0f };
    const Rectangle<float> kDisplay { 15.0f, 65.0f, 650.0f, 224.0f };
    const Rectangle<float> kPreset  { 200.0f, 10.0f, 250.0f, 35.0f };
    const Rectangle<float> kSave    { 461.0f, 10.0f, 38.0f, 35.0f };
    const Rectangle<float> kWave    { 185.0f, 124.0f, 330.0f, 88.0f };
    const Rectangle<float> kDragPill{ 559.0f, 150.0f, 86.0f, 36.0f };
    constexpr float kRowY = 262.0f, kLabelY = 556.0f, kFxY = 650.0f;
    constexpr float kEm = 1.32f;                // hauteur de police JUCE = taille em x 1,32 (Poppins)

    Colour scaled (Colour c, float k) { return Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, c.getFloatRed() * k), jlimit (0.0f, 1.0f, c.getFloatGreen() * k), jlimit (0.0f, 1.0f, c.getFloatBlue() * k), 1.0f); }

    // couleur braise le long de la montée (rouge sombre -> orange -> jaune)
    Colour emberAt (float t)
    {
        if (t < 0.6f)
        {
            const float k = t / 0.6f;
            return Colour::fromRGB ((uint8) (150 + 105 * k), (uint8) (40 + 82 * k), (uint8) (30 + 8 * k));
        }
        const float k = (t - 0.6f) / 0.4f;
        return Colour::fromRGB (255, (uint8) (122 + 90 * k), (uint8) (38 + 80 * k));
    }

    Image makeNoise()
    {
        Image im (Image::ARGB, 128, 128, true);
        Random r (1234);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
            {
                const float v = r.nextFloat();
                im.setPixelAt (x, y, v > 0.5f ? Colours::white.withAlpha ((v - 0.5f) * 0.05f) : Colours::black.withAlpha ((0.5f - v) * 0.065f));
            }
        return im;
    }
}

//==============================================================================
BackdraftEditor::BackdraftEditor (BackdraftProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    fonts.reg  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
    fonts.med  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
    fonts.bold = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    light      = Typeface::createSystemTypefaceFor (BinaryData::PoppinsLight_ttf, BinaryData::PoppinsLight_ttfSize);
    menuLnf.f = fonts;
    logo = Drawable::createFromImageData (BinaryData::NineHandsLogoWhite_svg, BinaryData::NineHandsLogoWhite_svgSize);

    nh::Knob::Layers L;
    L.shadow    = ImageCache::getFromMemory (BinaryData::knob_shadow_png,    BinaryData::knob_shadow_pngSize);
    L.crown     = ImageCache::getFromMemory (BinaryData::knob_crown_png,     BinaryData::knob_crown_pngSize);
    L.cap       = ImageCache::getFromMemory (BinaryData::knob_cap_png,       BinaryData::knob_cap_pngSize);
    L.marker    = ImageCache::getFromMemory (BinaryData::knob_marker_png,    BinaryData::knob_marker_pngSize);
    L.highlight = ImageCache::getFromMemory (BinaryData::knob_highlight_png, BinaryData::knob_highlight_pngSize);
    nh::Knob::Style ks;
    ks.dragPixels = 260.0f;
    ks.detents = true;            // crans francs avec résistance, sans flou de vitesse (demande de l'utilisateur)
    ks.blurMax = 0.0f;
    ks.hitRadius = 0.40f;
    knob = std::make_unique<nh::Knob> (*proc.apvts.getParameter ("length"), L, ks);
    knob->onRightClick = [this] (const MouseEvent&) { showParamMidiMenu ("length", "Reverse length"); };
    addAndMakeVisible (*knob);

    for (auto& f : lastFx) f = -1;
    for (int i = 0; i < numFx; ++i) fxLevel[i] = proc.fxParam[i]->load() > 0.5f ? 1.0f : 0.0f;

    setOpaque (true);
    setLookAndFeel (&menuLnf);          // coin de redimensionnement discret
    setResizable (true, true);
    setResizeLimits (kW, kH, kMaxW, kMaxH);
    getConstrainer()->setFixedAspectRatio ((double) kUW / kUH);
    setScaleIndex (nhsize::openIndex ("Backdraft", proc.uiSizeIdx.load(), 3));   // taille de l'instance, sinon taille par défaut choisie
    lastMidiAct = proc.midiActivity.load();
    startTimerHz (30);
}

BackdraftEditor::~BackdraftEditor() { stopTimer(); hostLock.detach(); setLookAndFeel (nullptr); }

Font BackdraftEditor::font (int w, float h) const
{
    const auto& tf = w == 0 ? light : w == 1 ? fonts.reg : w == 2 ? fonts.med : fonts.bold;
    return Font (FontOptions (tf).withHeight (h * kEm * sc()));
}

void BackdraftEditor::setScaleIndex (int idx)
{
    static const int ws[3] = { kW, 680, 907 };
    const int w = ws[jlimit (0, 2, idx)];
    setSize (w, roundToInt (w * kUH / kUW));
}

void BackdraftEditor::resized()
{
    const float s = sc();
    knob->setBounds (Rectangle<float> (kKnobBox * s, kKnobBox * s).withCentre (kKnobC * s).toNearestInt());
    layoutFx();
    resizeSettle = background.isValid() ? 3 : 1;      // le fond est reconstruit par le timer (anti-gel)
    if (! hostLock.hostMaximized()) hostLock.setMinimumSize (kW, kH);
}

//==============================================================================
//  Texte espacé (lettres écartées comme sur l'aperçu)
//==============================================================================
float BackdraftEditor::spacedWidth (const String& t, const Font& f, float spacing) const
{
    float w = 0;
    for (int i = 0; i < t.length(); ++i)
        w += GlyphArrangement::getStringWidth (f, String::charToString (t[i])) + (i < t.length() - 1 ? spacing * sc() : 0.0f);
    return w;
}

void BackdraftEditor::drawSpaced (Graphics& g, const String& t, const Font& f, float x, float y, float spacing, Justification j, Colour c) const
{
    const float s = sc();
    const float w = spacedWidth (t, f, spacing);
    float px = x * s;
    if (j.testFlags (Justification::horizontallyCentred)) px -= w * 0.5f;
    else if (j.testFlags (Justification::right)) px -= w;
    g.setColour (c);
    g.setFont (f);
    const float h = f.getHeight();
    for (int i = 0; i < t.length(); ++i)
    {
        const auto ch = String::charToString (t[i]);
        const float cw = GlyphArrangement::getStringWidth (f, ch);
        g.drawText (ch, Rectangle<float> (px - 2.0f, y * s - h * 0.5f, cw + 4.0f, h), Justification::centred, false);
        px += cw + spacing * s;
    }
}

//==============================================================================
//  Zones cliquables
//==============================================================================
void BackdraftEditor::layoutFx()
{
    const Font f (FontOptions (fonts.bold).withHeight (11.5f * kEm));
    float total = 0;
    for (int i = 0; i < numFx; ++i)
    {
        float w = 0;
        const String t (fxName (i));
        for (int k = 0; k < t.length(); ++k) w += GlyphArrangement::getStringWidth (f, String::charToString (t[k])) + (k < t.length() - 1 ? 1.5f : 0.0f);
        fxW[i] = 15.0f + w;
        total += fxW[i];
    }
    const float gap = 22.0f;
    total += gap * (numFx - 1);
    float x = kUW * 0.5f - total * 0.5f;
    for (int i = 0; i < numFx; ++i) { fxX[i] = x; x += fxW[i] + gap; }
}

Rectangle<float> BackdraftEditor::fxRect (int i) const { return { fxX[i] - 6.0f, kFxY - 14.0f, fxW[i] + 12.0f, 28.0f }; }

Rectangle<float> BackdraftEditor::zoneRect (Zone z) const
{
    switch (z)
    {
        case zPrev:    return { kPreset.getRight() - 74.0f, kPreset.getY(), 37.0f, kPreset.getHeight() };
        case zNext:    return { kPreset.getRight() - 37.0f, kPreset.getY(), 37.0f, kPreset.getHeight() };
        case zPreset:  return kPreset.withTrimmedRight (74.0f);
        case zSave:    return kSave;
        case zMenu:    return { 630.0f, 8.0f, 42.0f, 40.0f };
        case zVox:     return { 288.0f, 68.0f, 54.0f, 29.0f };
        case zInst:    return { 342.0f, 68.0f, 52.0f, 29.0f };
        case zReset:   return { 584.0f, 76.0f, 72.0f, 24.0f };
        case zListen:  return { 26.0f, kRowY - 12.0f, 72.0f, 24.0f };
        case zLoop:    return { 100.0f, kRowY - 12.0f, 62.0f, 24.0f };
        case zCapture: return { 486.0f, kRowY - 12.0f, 92.0f, 24.0f };
        case zImport:  return { 580.0f, kRowY - 12.0f, 76.0f, 24.0f };
        case zDrag:    return kDragPill;
        case zWave:    return kWave.expanded (6.0f, 0.0f);
        default: break;
    }
    if (z >= zFx0 && z <= zFx5) return fxRect ((int) z - (int) zFx0);
    return {};
}

BackdraftEditor::Zone BackdraftEditor::zoneAt (Point<float> p) const
{
    const auto u = p / sc();
    for (int z = zPrev; z <= zFx5; ++z)
        if (zoneRect ((Zone) z).contains (u)) return (Zone) z;
    return zNone;
}

//==============================================================================
//  Fond (reconstruit par le timer seulement, à la résolution physique de l'écran)
//==============================================================================
void BackdraftEditor::rebuildBackground()
{
    const float phys = jmax (1.0f, Component::getApproximateScaleFactorForComponent (this));
    const int W = jmax (1, roundToInt (getWidth() * phys)), H = jmax (1, roundToInt (getHeight() * phys));
    Image im (Image::RGB, W, H, true);
    Graphics g (im);
    const float s = sc() * phys;
    auto R = [s] (Rectangle<float> r) { return Rectangle<float> (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s); };

    g.fillAll (Colour (0xff141416));

    // ombre du boîtier sur la barre du haut
    Path body; body.addRoundedRectangle (R (kBody), 19.0f * s);
    DropShadow (Colours::black.withAlpha (0.6f), roundToInt (12 * s), { roundToInt (2 * s), roundToInt (4 * s) }).drawForPath (g, body);

    // façade : lumière d'ambiance à 45° (haut gauche) + reflet satiné perpendiculaire + grain
    const auto b = R (kBody);
    const Colour base (38, 38, 41);
    const float D = (b.getWidth() + b.getHeight()) * 0.5f;
    auto facadeFill = [&] (const Path& p)
    {
        ColourGradient cg (scaled (base, 1.10f), b.getX(), b.getY(), scaled (base, 0.86f), b.getX() + D, b.getY() + D, false);
        g.setGradientFill (cg);
        g.fillPath (p);
        ColourGradient sat (Colours::white.withAlpha (0.0f), b.getX(), b.getY(), Colours::white.withAlpha (0.0f), b.getX() + D, b.getY() + D, false);
        sat.addColour (0.20, Colours::white.withAlpha (0.0f));
        sat.addColour (0.32, Colours::white.withAlpha (12.0f / 255.0f));
        sat.addColour (0.44, Colours::white.withAlpha (0.0f));
        g.setGradientFill (sat);
        g.fillPath (p);
        static const Image noise = makeNoise();
        g.saveState();
        g.reduceClipRegion (p);
        g.setTiledImageFill (noise, 0, 0, 1.0f);
        g.fillRect (p.getBounds());
        g.restoreState();
    };
    facadeFill (body);
    // arête du boîtier : claire en haut à gauche, sombre en bas à droite
    {
        ColourGradient rim (Colours::white.withAlpha (0.22f), b.getX(), b.getY(), Colours::black.withAlpha (0.7f), b.getX() + D, b.getY() + D, false);
        rim.addColour (0.5, Colours::white.withAlpha (0.04f));
        g.setGradientFill (rim);
        g.strokePath (body, PathStrokeType (1.0f * s));
    }

    // écran encastré
    const auto d = R (kDisplay);
    Path disp; disp.addRoundedRectangle (d, 12.0f * s);
    g.setColour (Colour (17, 17, 19));
    g.fillPath (disp);
    {
        Path inv; inv.addRectangle (d.expanded (60 * s)); inv.addPath (disp); inv.setUsingNonZeroWinding (false);
        g.saveState();
        g.reduceClipRegion (disp);
        DropShadow (Colours::black.withAlpha (0.85f), roundToInt (8 * s), { roundToInt (2 * s), roundToInt (3 * s) }).drawForPath (g, inv);
        g.restoreState();
        // lèvre basse droite éclairée
        ColourGradient lip (Colours::white.withAlpha (0.0f), d.getX(), d.getY(), Colours::white.withAlpha (0.20f), d.getRight(), d.getBottom(), false);
        g.setGradientFill (lip);
        g.strokePath (disp, PathStrokeType (1.0f * s), AffineTransform::translation (0.5f * s, 0.5f * s));
    }

    // échancrure : la façade reprend autour du potard (même éclairage, raccord invisible)
    const auto kc = kKnobC * s;
    Path notch; notch.addEllipse (Rectangle<float> (kNotchR * 2 * s, kNotchR * 2 * s).withCentre (kc));
    {
        facadeFill (notch);
        // biseau léger
        ColourGradient bev (Colours::white.withAlpha (0.10f), kc.x - kNotchR * s * 0.7f, kc.y - kNotchR * s * 0.7f,
                            Colours::black.withAlpha (0.25f), kc.x + kNotchR * s * 0.7f, kc.y + kNotchR * s * 0.7f, false);
        g.setGradientFill (bev);
        g.strokePath (notch, PathStrokeType (1.2f * s));
    }

    // graduations : 6 crans (gros points) et 4 petits points entre chaque cran
    for (int i = 0; i <= (numLengths - 1) * 5; ++i)
    {
        const float a = degreesToRadians (-135.0f + 270.0f * (float) i / ((numLengths - 1) * 5));
        const Point<float> p (kc.x + kScaleR * s * std::sin (a), kc.y - kScaleR * s * std::cos (a));
        const bool big = (i % 5) == 0;
        const float r = (big ? 1.7f : 1.05f) * s;
        g.setColour (big ? Colour (225, 225, 230) : Colour (110, 110, 116));
        g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (p));
    }

    // grilles de points dans les quatre coins (trous encastrés)
    auto grill = [&] (float cx, float cy, float fx, float fy)
    {
        const int pat[5][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 }, { 0, 2 } };
        for (auto& q : pat)
        {
            const Point<float> c ((cx + q[0] * 17.0f * fx) * s, (cy + q[1] * 17.0f * fy) * s);
            const float r = 4.0f * s;
            const auto e = Rectangle<float> (r * 2, r * 2).withCentre (c);
            g.setColour (Colour (22, 22, 24)); g.fillEllipse (e);
            Path lo; lo.addCentredArc (c.x, c.y, r, r, 0.0f, degreesToRadians (30.0f), degreesToRadians (210.0f), true);
            g.setColour (Colour (80, 80, 86)); g.strokePath (lo, PathStrokeType (0.75f * s));
            Path hi; hi.addCentredArc (c.x, c.y, r - 0.5f * s, r - 0.5f * s, 0.0f, degreesToRadians (240.0f), degreesToRadians (370.0f), true);
            g.setColour (Colour (5, 5, 6)); g.strokePath (hi, PathStrokeType (1.0f * s));
        }
    };
    grill (18.0f, 305.0f, 1, 1);  grill (662.0f, 305.0f, -1, 1);
    grill (18.0f, 677.0f, 1, -1); grill (662.0f, 677.0f, -1, -1);

    // barre du haut : cadres fixes (presets, sauvegarde)
    auto box = [&] (Rectangle<float> r)
    {
        g.setColour (Colour (14, 14, 16)); g.fillRoundedRectangle (R (r), 5.0f * s);
        g.setColour (Colour (78, 78, 84)); g.drawRoundedRectangle (R (r).reduced (0.4f * s), 5.0f * s, 0.8f * s);
    };
    box (kPreset);
    box (kSave);
    g.setColour (Colour (78, 78, 84));
    g.fillRect (R ({ kPreset.getRight() - 74.0f, kPreset.getY(), 0.8f, kPreset.getHeight() }));
    g.fillRect (R ({ kPreset.getRight() - 37.0f, kPreset.getY(), 0.8f, kPreset.getHeight() }));
    // onglet VOX / INST
    g.setColour (Colour (12, 12, 14));
    g.fillRoundedRectangle (R ({ 285.0f, 68.0f, 110.0f, 29.0f }), 5.0f * s);

    background = im;
    bgW = getWidth(); bgH = getHeight(); bgScale = phys;
    rebuildGlows();
}

// halos des effets allumés (texte + icône floutés, dans la couleur braise)
void BackdraftEditor::rebuildGlows()
{
    const float phys = bgScale > 0 ? bgScale : 1.0f;
    const float s = sc() * phys;
    for (int i = 0; i < numFx; ++i)
    {
        const auto r = fxRect (i).expanded (14.0f, 10.0f);
        Image im (Image::ARGB, jmax (1, roundToInt (r.getWidth() * s)), jmax (1, roundToInt (r.getHeight() * s)), true);
        {
            Graphics g (im);
            g.addTransform (AffineTransform::translation (-r.getX() * s, -r.getY() * s));
            const Point<float> c ((fxX[i] + 5.5f) * s, kFxY * s);
            Path p; p.addCentredArc (c.x, c.y, 5.0f * s, 5.0f * s, 0.0f, degreesToRadians (30.0f), degreesToRadians (330.0f), true);
            g.setColour (kEmber);
            g.strokePath (p, PathStrokeType (2.6f * s));
            const Font f (FontOptions (fonts.bold).withHeight (11.5f * kEm * s));
            g.setFont (f);
            float x = (fxX[i] + 15.0f) * s;
            const String t (fxName (i));
            for (int k = 0; k < t.length(); ++k)
            {
                const auto ch = String::charToString (t[k]);
                const float cw = GlyphArrangement::getStringWidth (f, ch);
                g.drawText (ch, Rectangle<float> (x - 2, kFxY * s - f.getHeight() * 0.5f, cw + 4, f.getHeight()), Justification::centred, false);
                x += cw + 1.5f * s;
            }
        }
        const int rad = jmax (2, roundToInt (4.5f * s));
        ImageConvolutionKernel k (rad * 2 + 1);
        k.createGaussianBlur ((float) rad * 0.9f);
        k.applyToImage (im, im, im.getBounds());
        fxGlow[i] = im;
    }
}

//==============================================================================
//  Dessin
//==============================================================================
void BackdraftEditor::paint (Graphics& g)
{
    lastPaint = nowS();
    pendingSince = 0.0;
    g.fillAll (Colour (0xff141416));
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat());
    drawTopBar (g);
    drawDisplay (g);
    drawKnobArea (g);
    drawEffects (g);
}

void BackdraftEditor::drawTopBar (Graphics& g)
{
    const float s = sc();
    if (logo != nullptr)
    {
        const auto lr = U (17.0f, 27.5f - 150.0f / 22.0f, 150.0f, 150.0f / 11.0f);      // logo NINE HANDS | DSP agrandi
        logo->drawWithin (g, lr, RectanglePlacement::xLeft | RectanglePlacement::yMid, 0.82f);
    }
    // nom du preset / ligne d'état (même cadre)
    const auto pr = U (kPreset.getX() + 12.0f, kPreset.getY(), kPreset.getWidth() - 74.0f - 18.0f, kPreset.getHeight());
    String name = proc.presetName + (proc.presetDirty.load() ? " *" : "");
    if (statusAlpha < 0.999f)
    {
        g.setColour (Colour (222, 222, 226).withAlpha (1.0f - statusAlpha));
        g.setFont (font (2, 13.0f));
        g.drawFittedText (name, pr.toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
    if (statusAlpha > 0.001f)
    {
        g.setColour (kEmberHot.withAlpha (statusAlpha));
        g.setFont (font (2, 12.0f));
        nhstatus::draw (g, status, pr, Justification::centredLeft);   // chiffres à chasse fixe
    }
    // flèches
    for (int dir : { -1, 1 })
    {
        const auto r = U (dir < 0 ? kPreset.getRight() - 74.0f : kPreset.getRight() - 37.0f, kPreset.getY(), 37.0f, kPreset.getHeight());
        const bool hv = hover == (dir < 0 ? zPrev : zNext);
        Path p;
        const float cx = r.getCentreX(), cy = r.getCentreY(), k = 5.0f * s;
        p.startNewSubPath (cx - dir * k * 0.5f, cy - k); p.lineTo (cx + dir * k * 0.5f, cy); p.lineTo (cx - dir * k * 0.5f, cy + k);
        g.setColour (hv ? Colours::white : Colour (222, 222, 226));
        g.strokePath (p, PathStrokeType (1.2f * s, PathStrokeType::curved, PathStrokeType::rounded));
    }
    // sauvegarde (disquette +)
    {
        const auto c = U (kSave.getCentreX(), kSave.getCentreY(), 0, 0).getPosition();
        const Colour col = hover == zSave ? Colours::white : Colour (222, 222, 226);
        g.setColour (col);
        g.drawRoundedRectangle (Rectangle<float> (13 * s, 13 * s).withCentre (c), 1.5f * s, 1.1f * s);
        g.drawRect (Rectangle<float> (c.x - 3.5f * s, c.y - 6.5f * s, 6.0f * s, 4.0f * s), 1.0f * s);
        g.fillRect (Rectangle<float> (c.x + 2.0f * s, c.y + 1.5f * s, 4.0f * s, 4.0f * s));
        g.setColour (Colour (14, 14, 16));
        g.fillRect (Rectangle<float> (c.x + 2.6f * s, c.y + 2.1f * s, 2.8f * s, 2.8f * s));
        g.setColour (col);
        g.drawLine (c.x + 4.0f * s, c.y + 2.6f * s, c.x + 4.0f * s, c.y + 8.6f * s, 1.1f * s);
        g.drawLine (c.x + 1.0f * s, c.y + 5.6f * s, c.x + 7.0f * s, c.y + 5.6f * s, 1.1f * s);
    }
    // menu
    g.setColour (hover == zMenu ? Colours::white : Colour (200, 200, 204));
    for (int k = 0; k < 3; ++k)
        g.fillRoundedRectangle (U (642.0f, 20.9f + 6.0f * k, 17.0f, 1.2f), 0.6f * s);
}

void BackdraftEditor::drawDisplay (Graphics& g)
{
    const float s = sc();
    const float gl = glow();

    // titre
    {
        const Point<float> ic (36.0f * s, 88.0f * s);
        g.setColour (Colour (205, 205, 210));
        int k = 0;
        for (float r : { 8.0f, 5.5f, 3.0f })
        {
            Path p; p.addCentredArc (ic.x, ic.y, r * s, r * s, 0.0f, degreesToRadians (-160.0f + 30.0f * k), degreesToRadians (160.0f), true);
            g.strokePath (p, PathStrokeType (1.3f * s, PathStrokeType::curved, PathStrokeType::rounded));
            ++k;
        }
        const auto f1 = font (0, 15.5f), f2 = font (4, 15.5f);
        drawSpaced (g, "BACK", f1, 49.0f, 88.5f, 2.5f, Justification::left, Colour (150, 150, 156));
        const float w1 = spacedWidth ("BACK", f1, 2.5f) / s;
        drawSpaced (g, "DRAFT", f2, 49.0f + w1 + 4.5f, 88.5f, 2.5f, Justification::left, kTitle);
    }

    // VOX / INST
    const bool inst = proc.modeParam->load() > 0.5f;
    drawSpaced (g, "VOX",  font (inst ? 2 : 4, 12.0f), 318.0f, 83.5f, 1.0f, Justification::horizontallyCentred, inst ? (hover == zVox ? kLight : kGrey) : kEmber);
    drawSpaced (g, "INST", font (inst ? 4 : 2, 12.0f), 362.0f, 83.5f, 1.0f, Justification::horizontallyCentred, inst ? kEmber : (hover == zInst ? kLight : kGrey));

    // RESET
    {
        const Colour c = hover == zReset ? kLight : kGrey;
        drawSpaced (g, "RESET", font (2, 11.5f), 651.0f, 88.5f, 1.5f, Justification::right, c);
        const float wr = spacedWidth ("RESET", font (2, 11.5f), 1.5f) / s;
        const Point<float> ic ((651.0f - wr - 12.0f) * s, 88.0f * s);
        Path p; p.addCentredArc (ic.x, ic.y, 5.0f * s, 5.0f * s, 0.0f, degreesToRadians (-50.0f), degreesToRadians (240.0f), true);
        g.setColour (c);
        g.strokePath (p, PathStrokeType (1.2f * s, PathStrokeType::curved, PathStrokeType::rounded));
        Path a; a.addTriangle (ic.x - 6.6f * s, ic.y - 4.6f * s, ic.x - 1.4f * s, ic.y - 5.6f * s, ic.x - 4.6f * s, ic.y - 0.6f * s);
        g.fillPath (a);
    }

    // forme d'onde
    auto r = proc.currentRiser();
    const int capState = proc.captureState.load();
    const auto wv = U (kWave.getX(), kWave.getY(), kWave.getWidth(), kWave.getHeight());
    const float cy = wv.getCentreY(), amp = 37.0f * s;
    if (capState == 1)
    {
        // capture en cours : enveloppe en direct, en rouge
        const int n = (int) liveWave.size();
        const int bars = 96;
        for (int i = 0; i < bars; ++i)
        {
            const int k = n - bars + i;
            if (k < 0) continue;
            const float v = jlimit (0.0f, 1.0f, liveWave[(size_t) k]);
            const float x = wv.getX() + wv.getWidth() * (float) i / (bars - 1);
            const float h = jmax (1.0f * s, amp * v);
            g.setColour (kRec.withAlpha (0.35f + 0.65f * (float) i / bars));
            g.fillRoundedRectangle (x - 1.1f * s, cy - h, 2.2f * s, h * 2, 0.6f * s);
        }
        const double sec = proc.captureSeconds();
        drawSpaced (g, "REC  " + String (sec, 1) + " S", font (2, 11.5f), kUW * 0.5f, 120.0f, 1.5f, Justification::horizontallyCentred, kRec.withAlpha (0.55f + 0.45f * recBlink));
    }
    else if (r != nullptr && ! r->peaks.empty())
    {
        // halo braise derrière la montée (dégradé radial étiré en ellipse, sans bord)
        {
            const auto dr = U (kDisplay.getX(), kDisplay.getY(), kDisplay.getWidth(), kDisplay.getHeight());
            const Point<float> gc (440.0f * s, 168.0f * s);
            g.saveState();
            g.reduceClipRegion (dr.toNearestInt());
            g.addTransform (AffineTransform::scale (1.0f, 0.34f, gc.x, gc.y));
            ColourGradient rg (kEmber.withAlpha (0.30f * gl), gc.x, gc.y, kEmber.withAlpha (0.0f), gc.x + 190.0f * s, gc.y, true);
            rg.addColour (0.45, kEmber.withAlpha (0.12f * gl));
            g.setGradientFill (rg);
            g.fillRect (Rectangle<float> (380.0f * s, 380.0f * s).withCentre (gc));
            g.restoreState();
        }
        const int n = (int) r->peaks.size();
        const bool listen = proc.listening.load();
        const float played = listen ? (float) proc.playPos.load() / (float) jmax (1, r->riserLength) : 2.0f;
        const float dim = proc.rendering.load() ? 0.55f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (n - 1);
            const float x = wv.getX() + wv.getWidth() * t;
            const float v = std::pow (r->peaks[(size_t) i], 0.62f);       // échelle perceptive (la montée se voit dès le début)
            const float h = jmax (1.4f * s, amp * v);
            const float bw = (t < 0.5f ? 2.2f : 2.8f) * s;
            float a = (110.0f + 145.0f * t) / 255.0f * dim;
            if (listen && t > played) a *= 0.40f;
            g.setColour (emberAt (t).withAlpha (a));
            g.fillRoundedRectangle (x - bw * 0.5f, cy - h, bw, h * 2, 0.5f * s);
        }
        // repère de fin : la phrase commence ici
        const float ex = wv.getRight() + 7.0f * s;
        const bool phrase = listen && played >= 1.0f;
        g.setColour (phrase ? kEmberHot.withAlpha (0.85f) : Colours::white.withAlpha (0.16f));
        g.fillRect (Rectangle<float> (ex, cy - 44 * s, phrase ? 1.4f * s : 0.8f * s, 88 * s));
        if (listen && played < 1.0f)
        {
            const float px = wv.getX() + wv.getWidth() * jlimit (0.0f, 1.0f, played);
            g.setColour (Colours::white.withAlpha (0.55f));
            g.fillRect (Rectangle<float> (px, cy - 44 * s, 0.9f * s, 88 * s));
        }
    }
    else
    {
        // pas encore de source
        drawSpaced (g, proc.rendering.load() ? "RENDERING" : "CAPTURE OR IMPORT A PHRASE", font (2, 11.5f), kUW * 0.5f, 156.0f, 1.8f, Justification::horizontallyCentred, kGrey);
        drawSpaced (g, "or drop an audio file here", font (1, 10.5f), kUW * 0.5f, 178.0f, 0.6f, Justification::horizontallyCentred, kGrey.withAlpha (0.65f));
    }
    if (fileHover)
    {
        g.setColour (kEmber.withAlpha (0.6f));
        g.drawRoundedRectangle (U (kDisplay.getX() + 4, kDisplay.getY() + 4, kDisplay.getWidth() - 8, 140.0f), 8.0f * s, 1.2f * s);
    }

    // DRAG
    {
        const auto pr = U (kDragPill.getX(), kDragPill.getY(), kDragPill.getWidth(), kDragPill.getHeight());
        const bool on = r != nullptr && capState == 0;
        const bool hv = hover == zDrag && on;
        g.setColour (Colour (24, 24, 27));
        g.fillRoundedRectangle (pr, 7.0f * s);
        g.setColour (hv ? kEmber.withAlpha (0.8f) : Colour (70, 70, 76));
        g.drawRoundedRectangle (pr.reduced (0.4f * s), 7.0f * s, 0.8f * s);
        const Colour dc = on ? Colour (150, 150, 156) : Colour (70, 70, 76);
        g.setColour (dc);
        for (int rr = 0; rr < 3; ++rr)
            for (int c = 0; c < 2; ++c)
                g.fillEllipse (Rectangle<float> (2.2f * s, 2.2f * s).withCentre ({ pr.getX() + (14.0f + c * 5.0f) * s, pr.getCentreY() + (rr - 1) * 5.0f * s }));
        drawSpaced (g, "DRAG", font (3, 11.5f), kDragPill.getX() + 54.0f, kDragPill.getCentreY() + 0.5f, 1.5f, Justification::horizontallyCentred,
                    on ? (hv ? Colours::white : kLight) : Colour (90, 90, 96));
    }

    // LISTEN / LOOP
    {
        const bool listen = proc.listening.load();
        const Colour c = listen ? kEmber : (hover == zListen ? kLight : kGrey);
        if (listen)
        {
            g.setColour (c);
            g.fillRect (U (31.0f, kRowY - 5.0f, 3.2f, 10.0f));
            g.fillRect (U (36.6f, kRowY - 5.0f, 3.2f, 10.0f));
        }
        else
        {
            Path p; p.addTriangle (31.0f * s, (kRowY - 5.5f) * s, 31.0f * s, (kRowY + 5.5f) * s, 40.0f * s, kRowY * s);
            g.setColour (c); g.fillPath (p);
        }
        drawSpaced (g, "LISTEN", font (2, 11.5f), 46.0f, kRowY + 0.5f, 1.5f, Justification::left, c);

        const bool lp = proc.loop.load();
        const Colour lc = lp ? kEmber : (hover == zLoop ? kLight : kGrey);
        const Point<float> o (110.0f * s, kRowY * s);
        g.setColour (lc);
        Path a; a.addCentredArc (o.x, o.y, 6.5f * s, 4.5f * s, 0.0f, degreesToRadians (-70.0f), degreesToRadians (60.0f), true);
        Path b; b.addCentredArc (o.x, o.y, 6.5f * s, 4.5f * s, 0.0f, degreesToRadians (110.0f), degreesToRadians (240.0f), true);
        g.strokePath (a, PathStrokeType (1.2f * s)); g.strokePath (b, PathStrokeType (1.2f * s));
        Path h1; h1.addTriangle (o.x + 4.0f * s, o.y + 3.6f * s, o.x + 8.2f * s, o.y + 0.6f * s, o.x + 2.4f * s, o.y + 0.2f * s);
        Path h2; h2.addTriangle (o.x - 4.0f * s, o.y - 3.6f * s, o.x - 8.2f * s, o.y - 0.6f * s, o.x - 2.4f * s, o.y - 0.2f * s);
        g.fillPath (h1); g.fillPath (h2);
        drawSpaced (g, "LOOP", font (2, 11.5f), 121.0f, kRowY + 0.5f, 1.5f, Justification::left, lc);
    }

    // CAPTURE / IMPORT
    {
        const float rightX = 653.0f;
        const auto fi = font (2, 11.5f);
        const float wImp = spacedWidth ("IMPORT", fi, 1.5f) / s;
        const Colour ic = hover == zImport ? kLight : kGrey;
        drawSpaced (g, "IMPORT", fi, rightX, kRowY + 0.5f, 1.5f, Justification::right, ic);
        const float ia = rightX - wImp - 9.0f;
        g.setColour (ic);
        Path tray;
        tray.startNewSubPath ((ia - 4.5f) * s, (kRowY + 1.0f) * s); tray.lineTo ((ia - 4.5f) * s, (kRowY + 4.5f) * s);
        tray.lineTo ((ia + 4.5f) * s, (kRowY + 4.5f) * s); tray.lineTo ((ia + 4.5f) * s, (kRowY + 1.0f) * s);
        g.strokePath (tray, PathStrokeType (1.1f * s, PathStrokeType::curved, PathStrokeType::rounded));
        g.drawLine (ia * s, (kRowY - 5.5f) * s, ia * s, (kRowY + 1.0f) * s, 1.1f * s);
        Path ar; ar.addTriangle ((ia - 3.0f) * s, (kRowY - 1.0f) * s, (ia + 3.0f) * s, (kRowY - 1.0f) * s, ia * s, (kRowY + 2.5f) * s);
        g.fillPath (ar);

        const bool rec = capState == 1;
        const String capT = rec ? "STOP" : "CAPTURE";
        const float capRight = ia - 16.0f;
        drawSpaced (g, capT, fi, capRight, kRowY + 0.5f, 1.5f, Justification::right, rec ? kRec : (hover == zCapture ? Colours::white : kLight));
        const float wCap = spacedWidth (capT, fi, 1.5f) / s;
        const Point<float> dc ((capRight - wCap - 10.0f) * s, kRowY * s);
        const float lvl = rec ? (0.55f + 0.45f * recBlink) : 0.75f;
        ColourGradient halo (kRec.withAlpha (0.5f * gl * lvl), dc.x, dc.y, kRec.withAlpha (0.0f), dc.x + 9.0f * s, dc.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (Rectangle<float> (18.0f * s, 18.0f * s).withCentre (dc));
        g.setColour (kRec.withAlpha (rec ? 0.6f + 0.4f * recBlink : 1.0f));
        if (rec) g.fillRoundedRectangle (Rectangle<float> (7.0f * s, 7.0f * s).withCentre (dc), 1.2f * s);
        else     g.fillEllipse (Rectangle<float> (8.0f * s, 8.0f * s).withCentre (dc));
    }
}

void BackdraftEditor::drawKnobArea (Graphics& g)
{
    const int li = jlimit (0, numLengths - 1, (int) std::round (proc.lengthParam->load()));
    const auto fl = font (4, 14.5f), fv = font (2, 14.5f);
    const String val = lengthName (li);
    const float s = sc();
    const float w1 = spacedWidth ("REVERSE", fl, 1.5f) / s, w2 = spacedWidth (val, fv, 1.5f) / s;
    const float x0 = kKnobC.x - (w1 + 7.0f + w2) * 0.5f;
    drawSpaced (g, "REVERSE", fl, x0, kLabelY, 1.5f, Justification::left, Colour (226, 226, 230));
    drawSpaced (g, val, fv, x0 + w1 + 7.0f, kLabelY, 1.5f, Justification::left, Colour (120, 120, 126));
}

void BackdraftEditor::drawEffects (Graphics& g)
{
    const float s = sc();
    const auto f = font (4, 11.5f);
    for (int i = 0; i < numFx; ++i)
    {
        const float lv = fxLevel[i];
        if (lv > 0.001f && fxGlow[i].isValid())
        {
            const auto r = fxRect (i).expanded (14.0f, 10.0f);
            g.setOpacity (jlimit (0.0f, 1.0f, lv * (0.55f + 0.6f * glow())));
            g.drawImage (fxGlow[i], U (r.getX(), r.getY(), r.getWidth(), r.getHeight()));
            g.setOpacity (1.0f);
        }
        const bool hv = hover == (Zone) (zFx0 + i);
        const Colour off = hv ? kLight : kGrey;
        const Colour on (255, 150, 70);
        const Colour c = off.interpolatedWith (on, lv);
        const Colour ic = off.interpolatedWith (kEmber, lv);
        const Point<float> cc ((fxX[i] + 5.5f) * s, kFxY * s);
        Path p; p.addCentredArc (cc.x, cc.y, 5.0f * s, 5.0f * s, 0.0f, degreesToRadians (30.0f), degreesToRadians (330.0f), true);
        g.setColour (ic);
        g.strokePath (p, PathStrokeType (1.2f * s, PathStrokeType::curved, PathStrokeType::rounded));
        g.drawLine (cc.x, cc.y - 7.0f * s, cc.x, cc.y - 1.0f * s, 1.2f * s);
        drawSpaced (g, fxName (i), f, fxX[i] + 15.0f, kFxY + 0.5f, 1.5f, Justification::left, c);
    }
}

//==============================================================================
//  Souris
//==============================================================================
void BackdraftEditor::mouseMove (const MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
    setMouseCursor (z == zDrag && proc.currentRiser() != nullptr ? MouseCursor::DraggingHandCursor
                    : z != zNone ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void BackdraftEditor::mouseExit (const MouseEvent&) { if (hover != zNone) { hover = zNone; repaint(); } }

void BackdraftEditor::mouseDown (const MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    pressZone = z; pressPos = e.position; dragStarted = false;
    if (statusAlpha > 0.0f && z == zPreset) { statusTime = -10.0; }

    if (e.mods.isPopupMenu())
    {
        if (z >= zFx0 && z <= zFx5) { const int i = (int) z - (int) zFx0; String n (fxName (i)); showParamMidiMenu (fxId (i), n.substring (0, 1) + n.substring (1).toLowerCase()); }
        else if (z == zVox || z == zInst) showParamMidiMenu ("mode", "Vox / Inst");
        else showMainMenu();
        return;
    }
    switch (z)
    {
        case zPrev:    prevNextPreset (-1); break;
        case zNext:    prevNextPreset (1); break;
        case zPreset:  showPresetMenu(); break;
        case zSave:    savePreset(); break;
        case zMenu:    showMainMenu(); break;
        case zVox:     setMode (false); break;
        case zInst:    setMode (true); break;
        case zReset:   proc.resetToDefault(); showStatus ("RESET  " + proc.presetName.toUpperCase()); break;
        case zListen:  proc.toggleListen(); break;
        case zLoop:    proc.loop = ! proc.loop.load(); showStatus (proc.loop.load() ? "LOOP ON" : "LOOP OFF"); break;
        case zCapture:
            if (proc.captureState.load() == 1) { proc.stopCapture(); showStatus ("CAPTURE DONE  " + String (proc.captureSeconds(), 1) + " S"); }
            else { liveWave.clear(); proc.startCapture(); showStatus ("CAPTURE  PLAY THE TRACK, CLICK STOP"); }
            break;
        case zImport:  importDialog(); break;
        default:
            if (z >= zFx0 && z <= zFx5) toggleFx ((int) z - (int) zFx0);
            break;
    }
    repaint();
}

void BackdraftEditor::mouseDrag (const MouseEvent& e)
{
    if ((pressZone == zDrag || pressZone == zWave) && ! dragStarted && e.position.getDistanceFrom (pressPos) > 4.0f)
    {
        dragStarted = true;
        startDragOut();
    }
}

void BackdraftEditor::mouseUp (const MouseEvent&)
{
    if (pressZone == zDrag && ! dragStarted && proc.currentRiser() != nullptr)
        showStatus ("DRAG THE RISER ONTO THE TIMELINE");
    pressZone = zNone;
}

void BackdraftEditor::startDragOut()
{
    if (proc.captureState.load() == 1) return;
    const auto f = proc.exportRiserFile();
    if (! f.existsAsFile()) { showStatus ("NOTHING TO DRAG YET"); return; }
    showStatus ("RISER  " + f.getFileNameWithoutExtension());
    DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

bool BackdraftEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void BackdraftEditor::filesDropped (const StringArray& files, int, int)
{
    fileHover = false;
    for (auto& fn : files)
    {
        String err;
        if (proc.importFile (File (fn), err)) { showStatus ("IMPORTED  " + File (fn).getFileNameWithoutExtension()); break; }
        showStatus ("IMPORT FAILED  " + err);
    }
    repaint();
}

void BackdraftEditor::importDialog()
{
    chooser = std::make_unique<FileChooser> ("Import a phrase", BackdraftProcessor::userFolder ("Captures"), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        String err;
        if (proc.importFile (f, err)) showStatus ("IMPORTED  " + f.getFileNameWithoutExtension());
        else showStatus ("IMPORT FAILED  " + err);
        repaint();
    });
}

void BackdraftEditor::toggleFx (int i)
{
    if (auto* p = proc.apvts.getParameter (fxId (i)))
    {
        const bool on = p->getValue() < 0.5f;
        p->beginChangeGesture(); p->setValueNotifyingHost (on ? 1.0f : 0.0f); p->endChangeGesture();
    }
}

void BackdraftEditor::setMode (bool inst)
{
    if (auto* p = proc.apvts.getParameter ("mode"))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost (inst ? 1.0f : 0.0f); p->endChangeGesture();
    }
}

//==============================================================================
//  Presets et menus (style Octopus, toujours ouverts là où on clique)
//==============================================================================
void BackdraftEditor::showStatus (const String& s) { status = s; statusTime = nowS(); }

void BackdraftEditor::prevNextPreset (int dir)
{
    const int n = (int) BackdraftProcessor::factoryPresets().size();
    const int cur = proc.currentPreset.load();
    proc.loadFactoryPreset (((cur < 0 ? 0 : cur) + dir + n) % n);
    statusTime = -10.0;
    repaint();
}

void BackdraftEditor::savePreset()
{
    chooser = std::make_unique<FileChooser> ("Save Backdraft preset", BackdraftProcessor::userFolder ("Presets").getChildFile (proc.presetName + ".backdraft"), "*.backdraft");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        f = f.withFileExtension ("backdraft");
        if (auto xml = proc.savePresetTree().createXml()) xml->writeTo (f);
        proc.presetName = f.getFileNameWithoutExtension();
        proc.presetDirty = false; proc.currentPreset = -1;
        showStatus ("PRESET SAVED");
        repaint();
    });
}

void BackdraftEditor::loadPreset()
{
    chooser = std::make_unique<FileChooser> ("Load Backdraft preset", BackdraftProcessor::userFolder ("Presets"), "*.backdraft");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        if (auto xml = XmlDocument::parse (f))
            proc.loadPresetTree (ValueTree::fromXml (*xml), f.getFileNameWithoutExtension());
        repaint();
    });
}

void BackdraftEditor::showPresetMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    BackdraftMenu::addHeader (m, fonts, BackdraftMenu::icoPreset, kEmber, "Presets", proc.presetName);
    const auto& fp = BackdraftProcessor::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) m.addItem (1 + i, fp[(size_t) i].name, true, proc.currentPreset.load() == i && ! proc.presetDirty.load());
    // presets de l'utilisateur
    Array<File> user = BackdraftProcessor::userFolder ("Presets").findChildFiles (File::findFiles, false, "*.backdraft");
    user.sort();
    if (! user.isEmpty())
    {
        m.addSeparator();
        for (int i = 0; i < jmin (60, user.size()); ++i)
            m.addItem (200 + i, user[i].getFileNameWithoutExtension(), true, proc.currentPreset.load() < 0 && proc.presetName == user[i].getFileNameWithoutExtension());
    }
    m.addSeparator();
    m.addItem (90, "Save preset...");
    m.addItem (91, "Load preset...");
    m.showMenuAsync (menuAt(), [this, user] (int r)
    {
        if (r >= 1 && r < 90) proc.loadFactoryPreset (r - 1);
        else if (r == 90) savePreset();
        else if (r == 91) loadPreset();
        else if (r >= 200 && r - 200 < user.size())
            if (auto xml = XmlDocument::parse (user[r - 200]))
                proc.loadPresetTree (ValueTree::fromXml (*xml), user[r - 200].getFileNameWithoutExtension());
        statusTime = -10.0;
        repaint();
    });
}

void BackdraftEditor::showParamMidiMenu (const String& paramId, const String& title)
{
    const int pi = proc.paramIndexOf (paramId);
    if (pi < 0) return;
    const int cc = proc.ccForParam (pi);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    BackdraftMenu::addHeader (m, fonts, BackdraftMenu::icoMidi, kEmber, title, cc >= 0 ? "MIDI CC " + String (cc) : String ("No MIDI CC"));
    m.addItem (1, "MIDI Learn", true, proc.learnParam.load() == pi);
    if (cc >= 0) m.addItem (2, "Clear MIDI");
    if (proc.learnParam.load() >= 0) m.addItem (3, "Cancel MIDI learn");
    m.showMenuAsync (menuAt(), [this, pi] (int r)
    {
        if (r == 1) proc.learnParam = pi;
        else if (r == 2) proc.clearMidiFor (pi);
        else if (r == 3) proc.learnParam = -1;
    });
}

void BackdraftEditor::showMainMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    BackdraftMenu::addHeader (m, fonts, BackdraftMenu::icoRiser, kEmber, "Backdraft", "Reverse reverb riser  v" + String (JucePlugin_VersionString));
    PopupMenu pres, midi, size, gl;
    const auto& fp = BackdraftProcessor::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) pres.addItem (1 + i, fp[(size_t) i].name, true, proc.currentPreset.load() == i && ! proc.presetDirty.load());
    pres.addSeparator();
    pres.addItem (90, "Save preset...");
    pres.addItem (91, "Load preset...");
    midi.addItem (120, "Right-click a control to learn", false, false);
    if (proc.learnParam.load() >= 0) midi.addItem (121, "Cancel MIDI learn");
    midi.addItem (122, "Clear all MIDI");
    static const char* sz[3] = { "60 %", "75 %", "100 %" };
    const int defSz = nhsize::defaultIndex ("Backdraft", 3);
    const int cur = nhsize::openIndex ("Backdraft", proc.uiSizeIdx.load(), 3);
    PopupMenu defSizes;
    for (int i = 0; i < 3; ++i)
    {
        size.addItem (130 + i, String (sz[i]) + (i == defSz ? "  (default)" : ""), true, cur == i);
        defSizes.addItem (nhsize::idDefault + i, sz[i], true, i == defSz);
    }
    size.addSeparator();
    size.addSubMenu ("Default size", defSizes);     // taille des nouvelles fenêtres, commune à toutes les instances
    for (int i = 0; i < 4; ++i) gl.addItem (140 + i, String ((i + 1) * 25) + " %", true, proc.glowIdx.load() == i);
    m.addSubMenu ("Presets", pres);
    m.addItem (150, "Import audio file...");
    m.addItem (151, "Open risers folder");
    m.addItem (152, "Open captures folder");
    m.addSeparator();
    m.addSubMenu ("MIDI", midi);
    m.addSubMenu ("Plugin size", size);
    m.addSubMenu ("Glow intensity", gl);
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r >= 1 && r < 90) proc.loadFactoryPreset (r - 1);
        else if (r == 90) savePreset();
        else if (r == 91) loadPreset();
        else if (r == 121) proc.learnParam = -1;
        else if (r == 122) for (auto& x : proc.ccMap) x = -1;
        else if (r >= 130 && r < 133) { proc.uiSizeIdx = r - 130; setScaleIndex (r - 130); }
        else if (r >= nhsize::idDefault && r < nhsize::idDefault + 3) { nhsize::setDefaultIndex ("Backdraft", r - nhsize::idDefault); proc.uiSizeIdx = -1; setScaleIndex (r - nhsize::idDefault); }
        else if (r >= 140 && r < 144) proc.glowIdx = r - 140;
        else if (r == 150) importDialog();
        else if (r == 151) BackdraftProcessor::userFolder ("Risers").startAsProcess();
        else if (r == 152) BackdraftProcessor::userFolder ("Captures").startAsProcess();
        statusTime = r >= 1 && r < 90 ? -10.0 : statusTime;
        repaint();
    });
}

//==============================================================================
//  Anti-gel v2
//==============================================================================
void BackdraftEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if ((void*) peer != lastPeer) { lastPeer = (void*) peer; repaint(); }
    lastRendererCheck = nowS();
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw) peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void BackdraftEditor::trackHostWindow()
{
    if (auto* peer = getPeer())
    {
        void* handle = peer->getNativeHandle();
        if (handle != nullptr && ! hostLock.isAttachedTo (handle))
        {
            Component::SafePointer<Component> top (&peer->getComponent());
            hostLock.attach (handle, [top] { return top != nullptr ? top->getWidth() : 0; });
            hostLock.setMinimumSize (kW, kH);
            hostLock.setMaximumSize (kMaxW, kMaxH);
            hostLock.enforceNow();
        }
        if (getWidth() < kW - 2) hostLock.enforceNow();
    }
    else hostLock.detach();
}

void BackdraftEditor::parentHierarchyChanged() { useSoftwareRenderer(); trackHostWindow(); }
void BackdraftEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

void BackdraftEditor::askRepaint (Rectangle<int> r)
{
    if (pendingSince <= 0.0) pendingSince = nowS();
    repaint (r);
}

//==============================================================================
//  Timer 30 Hz : ne redessine que ce qui change
//==============================================================================
void BackdraftEditor::timerCallback()
{
    const double t = nowS();
    if ((void*) getPeer() != lastPeer || t - lastRendererCheck > 1.0) { useSoftwareRenderer(); trackHostWindow(); }
    if (pendingSince > 0.0 && t - pendingSince > 1.5 && isShowing())                 // chien de garde
    {
        lastPeer = nullptr; useSoftwareRenderer(); repaint(); pendingSince = 0.0;
    }

    // fond : reconstruit après un redimensionnement ou un changement d'échelle (jamais dans paint)
    if (t - lastScaleCheck > 0.25)
    {
        lastScaleCheck = t;
        const float sf = jmax (1.0f, Component::getApproximateScaleFactorForComponent (this));
        if (std::abs (sf - lastScaleFactor) > 0.01f && resizeSettle == 0) resizeSettle = 1;
        lastScaleFactor = sf;
    }
    if (resizeSettle > 0 && --resizeSettle == 0) { rebuildBackground(); repaint(); }

    const float s = sc();
    auto area = [&] (Rectangle<float> u) { return Rectangle<float> (u.getX() * s, u.getY() * s, u.getWidth() * s, u.getHeight() * s).getSmallestIntegerContainer().expanded (2); };

    // réglages -> ligne d'état + zones à redessiner
    const int li = jlimit (0, numLengths - 1, (int) std::round (proc.lengthParam->load()));
    if (li != lastLength)
    {
        if (lastLength >= 0) showStatus ("REVERSE  " + lengthName (li) + "  " + String (roundToInt (proc.hostBpm.load())) + " BPM");
        lastLength = li;
        askRepaint (area ({ 150.0f, kLabelY - 14.0f, 380.0f, 28.0f }));
    }
    const int md = proc.modeParam->load() > 0.5f ? 1 : 0;
    if (md != lastMode)
    {
        if (lastMode >= 0) showStatus (md ? "INST MODE  INSTRUMENTS, NO DE-ESSER" : "VOX MODE  AUTO DE-ESSER");
        lastMode = md;
        askRepaint (area ({ 285.0f, 68.0f, 110.0f, 29.0f }));
    }
    for (int i = 0; i < numFx; ++i)
    {
        const int on = proc.fxParam[i]->load() > 0.5f ? 1 : 0;
        if (on != lastFx[i])
        {
            if (lastFx[i] >= 0) showStatus (String (fxName (i)) + (on ? "  ON" : "  OFF"));
            lastFx[i] = on;
        }
        const float tgt = (float) on;
        // fondu vers l'état allumé / éteint, puis immobile (pas d'aller-retour qui ferait scintiller)
        const float nv = tgt > fxLevel[i] ? jmin (tgt, fxLevel[i] + 0.45f) : jmax (tgt, fxLevel[i] - 0.28f);
        if (std::abs (nv - fxLevel[i]) > 0.001f) { fxLevel[i] = nv; askRepaint (area (fxRect (i).expanded (16.0f, 12.0f))); }
    }
    const int rc = proc.renderCount.load();
    const bool rend = proc.rendering.load();
    if (rc != lastRenderCount || rend != lastRendering) { lastRenderCount = rc; lastRendering = rend; askRepaint (area (kDisplay)); }
    if (proc.glowIdx.load() != lastGlow) { lastGlow = proc.glowIdx.load(); repaint(); }

    // écoute
    const bool ls = proc.listening.load();
    if (ls != lastListen || proc.loop.load() != lastLoop) { lastListen = ls; lastLoop = proc.loop.load(); askRepaint (area (kDisplay)); }
    if (ls)
    {
        const int pp = proc.playPos.load();
        if (pp != lastPlay) { lastPlay = pp; askRepaint (area (kWave.expanded (12.0f, 10.0f))); }
    }

    // capture
    const int cs = proc.captureState.load();
    if (cs == 1)
    {
        liveWave.push_back (jlimit (0.0f, 1.0f, std::pow (proc.captureLevel(), 0.6f)));
        if (liveWave.size() > 400) liveWave.erase (liveWave.begin(), liveWave.begin() + 100);
        recBlink = 0.5f + 0.5f * std::sin ((float) t * 6.0f);
        askRepaint (area (kDisplay));
    }
    if (cs != lastCapture)
    {
        if (lastCapture == 1 && cs == 0 && t - statusTime > 0.2) showStatus ("CAPTURE DONE  " + String (proc.captureSeconds(), 1) + " S");
        lastCapture = cs; askRepaint (area (kDisplay));
    }

    // MIDI Learn
    const int lp = proc.learnParam.load();
    if (lp >= 0) showStatus ("MIDI LEARN  " + proc.getParameters()[lp]->getName (40));
    const int ll = proc.lastLearnedCc.exchange (-1);
    if (ll >= 0)
    {
        const int pi = proc.ccMap[ll].load();
        if (pi >= 0) showStatus ("CC " + String (ll) + "  " + proc.getParameters()[pi]->getName (40));
    }

    // fondu de la ligne d'état (même cadre que le nom du preset)
    const float target = (t - statusTime < 2.5) ? 1.0f : 0.0f;
    const float na = target > statusAlpha ? jmin (target, statusAlpha + 0.28f) : jmax (target, statusAlpha - 0.11f);   // immobile une fois arrivé (pas de scintillement)
    if (std::abs (na - statusAlpha) > 0.001f || (statusAlpha > 0 && status != lastStatusDrawn))
    {
        statusAlpha = na; lastStatusDrawn = status;
        askRepaint (area (kPreset));
    }
    const String pn = proc.presetName + (proc.presetDirty.load() ? "*" : "");
    if (pn != lastPresetDrawn) { lastPresetDrawn = pn; askRepaint (area (kPreset)); }
}
