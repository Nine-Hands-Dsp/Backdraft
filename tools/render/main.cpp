// Backdraft hors plugin (tools/render) : test du moteur sur une phrase de synthèse + captures de l'interface
#include "PluginEditor.h"
#include <random>
using namespace juce;

static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// phrase « chantée » de synthèse : « s » sifflant puis voyelle avec formants et vibrato
static AudioBuffer<float> makePhrase (double sr)
{
    const int n = (int) (2.2 * sr);
    AudioBuffer<float> b (2, n); b.clear();
    std::mt19937 rng (3); std::normal_distribution<float> nd (0.0f, 1.0f);
    backdraft::Biquad sF, f1, f2, f3;
    sF.highPass (sr, 6000, 0.9);
    f1.peak (sr, 700, 4, 18); f2.peak (sr, 1200, 5, 14); f3.peak (sr, 2600, 6, 10);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double x = 0;
        if (t > 0.30 && t < 0.42) x += sF.process (nd (rng)) * 0.5 * std::sin (MathConstants<double>::pi * (t - 0.30) / 0.12);   // « s »
        if (t > 0.42 && t < 1.9)
        {
            const double f0 = 196 * (1 + 0.012 * std::sin (2 * MathConstants<double>::pi * 5.5 * t));
            ph += f0 / sr; if (ph > 1) ph -= 1;
            const double saw = 2 * ph - 1;
            const double env = std::min (1.0, (t - 0.42) / 0.03) * std::min (1.0, (1.9 - t) / 0.2);
            x += env * 0.08 * f3.process (f2.process (f1.process (saw)));
        }
        b.setSample (0, i, (float) x); b.setSample (1, i, (float) x);
    }
    return b;
}

static void writeWav (const AudioBuffer<float>& b, int len, double sr, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    auto os = std::unique_ptr<OutputStream> (f.createOutputStream());
    if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (b, 0, len); }
}

static void snap (Component& c, const String& name, float scale)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    File out = File::getCurrentWorkingDirectory().getChildFile (name);
    out.deleteFile();
    FileOutputStream os (out); PNGImageFormat png; png.writeImageToStream (img, os);
    std::cout << "ecrit " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
}

static bool waitRender (BackdraftProcessor& p, int prevCount, int timeoutMs = 20000)
{
    const auto t0 = Time::getMillisecondCounter();
    while (p.renderCount.load() == prevCount && (int) (Time::getMillisecondCounter() - t0) < timeoutMs) pump (20);
    return p.renderCount.load() != prevCount;
}

static void analyse (const backdraft::Riser& r, const String& label)
{
    const int L = r.riserLength;
    String s = label + " : " + String ((double) L / r.sampleRate, 2) + " s, RMS par quart (dB) :";
    for (int q = 0; q < 4; ++q)
    {
        double sum = 0; const int a = q * L / 4, e = (q + 1) * L / 4;
        for (int i = a; i < e; ++i) sum += (double) r.audio.getSample (0, i) * r.audio.getSample (0, i);
        s << " " << String (10 * std::log10 (sum / jmax (1, e - a) + 1e-12), 1);
    }
    float pk = 0; for (int i = 0; i < L; ++i) pk = jmax (pk, std::abs (r.audio.getSample (0, i)), std::abs (r.audio.getSample (1, i)));
    // sifflantes : énergie > 6 kHz dans le dernier quart
    backdraft::Biquad h; h.highPass (r.sampleRate, 6000, 0.7071);
    double hi = 0, all = 0;
    for (int i = 3 * L / 4; i < L; ++i) { const double x = r.audio.getSample (0, i); const double y = h.process (x); hi += y * y; all += x * x; }
    s << "  crete " << String (20 * std::log10 (pk), 2) << " dB, part > 6 kHz (fin) " << String (100.0 * hi / jmax (1e-12, all), 1) << " %";
    std::cout << s << "\n";
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const auto dir = File::getCurrentWorkingDirectory();

    BackdraftProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 512);
    p.prepareToPlay (sr, 512);
    pump (100);

    // source de test
    auto phrase = makePhrase (sr);
    const auto src = dir.getChildFile ("test_phrase.wav");
    writeWav (phrase, phrase.getNumSamples(), sr, src);
    String err;
    int c0 = p.renderCount.load();
    if (! p.importFile (src, err)) { std::cout << "import KO " << err << "\n"; return 1; }
    if (! waitRender (p, c0)) { std::cout << "rendu KO\n"; return 1; }
    analyse (*p.currentRiser(), "Ignition VOX 4 bars");
    writeWav (p.currentRiser()->audio, p.currentRiser()->audio.getNumSamples(), sr, dir.getChildFile ("riser_ignition_ecoute.wav"));

    // chaque preset d'usine
    for (int i = 0; i < (int) BackdraftProcessor::factoryPresets().size(); ++i)
    {
        c0 = p.renderCount.load();
        p.loadFactoryPreset (i);
        if (! waitRender (p, c0)) { std::cout << "rendu KO preset " << i << "\n"; continue; }
        auto r = p.currentRiser();
        analyse (*r, BackdraftProcessor::factoryPresets()[(size_t) i].name);
        writeWav (r->audio, r->riserLength, sr, dir.getChildFile ("riser_" + String (i) + ".wav"));
    }
    // VOX vs INST : effet de l'anti-sifflantes
    c0 = p.renderCount.load(); p.loadFactoryPreset (0); waitRender (p, c0); analyse (*p.currentRiser(), "VOX (anti-sifflantes)");
    c0 = p.renderCount.load(); p.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f); p.requestRender(); waitRender (p, c0); analyse (*p.currentRiser(), "INST (sans)");
    p.loadFactoryPreset (0); pump (300);

    // écoute : le riser passe dans la sortie
    {
        p.toggleListen();
        AudioBuffer<float> buf (2, 512); MidiBuffer m; double e = 0;
        for (int k = 0; k < 200; ++k) { buf.clear(); p.processBlock (buf, m); e += buf.getRMSLevel (0, 0, 512); }
        std::cout << "ecoute : niveau moyen " << e / 200 << (e > 0 ? " (OK)" : " (MUET !)") << "\n";
        p.stopListen();
        for (int k = 0; k < 4; ++k) { buf.clear(); p.processBlock (buf, m); }   // fin du fondu
    }
    // capture : 1,5 s de phrase passent dans le plugin
    {
        c0 = p.renderCount.load();
        p.startCapture();
        MidiBuffer m;
        for (int s0 = 0; s0 + 512 <= phrase.getNumSamples(); s0 += 512)
        {
            AudioBuffer<float> blk (2, 512);
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, phrase, c, s0, 512);
            p.processBlock (blk, m);
            // le signal traverse sans modification
            if (std::abs (blk.getSample (0, 100) - phrase.getSample (0, s0 + 100)) > 1e-6f) { std::cout << "passage KO\n"; break; }
        }
        p.stopCapture();
        std::cout << "capture : " << p.captureSeconds() << " s, rendu " << (waitRender (p, c0) ? "OK" : "KO") << "\n";
    }

    // export du fichier glissé
    const auto f = p.exportRiserFile();
    std::cout << "export : " << f.getFullPathName() << " " << f.getSize() << " octets\n";

    // interface
    std::unique_ptr<BackdraftEditor> ed (dynamic_cast<BackdraftEditor*> (p.createEditor()));
    ed->setVisible (true);
    for (int k = 0; k < 6; ++k) { ed->timerCallback(); pump (20); }
    snap (*ed, "ui_60.png", 1.0f);
    ed->setSize (680, 700); for (int k = 0; k < 6; ++k) { ed->timerCallback(); pump (20); }
    snap (*ed, "ui_75.png", 1.0f);
    // effets allumés + écoute en cours
    for (auto id : { "chorus", "shimmer", "drive", "tremolo" }) p.apvts.getParameter (id)->setValueNotifyingHost (1.0f);
    c0 = p.renderCount.load(); waitRender (p, c0);
    p.toggleListen();
    { AudioBuffer<float> buf (2, 512); MidiBuffer m; for (int k = 0; k < 120; ++k) { buf.clear(); p.processBlock (buf, m); } }
    for (int k = 0; k < 10; ++k) { ed->timerCallback(); pump (20); }
    snap (*ed, "ui_fx_listen.png", 1.0f);
    p.stopListen();
    // capture en cours
    p.startCapture();
    { MidiBuffer m; for (int k = 0; k < 60; ++k) { AudioBuffer<float> blk (2, 512); for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, phrase, c, 20000 + k * 512, 512); p.processBlock (blk, m); ed->timerCallback(); } }
    snap (*ed, "ui_capture.png", 1.0f);
    p.stopCapture(); pump (200);
    // sans source
    {
        BackdraftProcessor p2; p2.setPlayConfigDetails (2, 2, sr, 512); p2.prepareToPlay (sr, 512);
        std::unique_ptr<BackdraftEditor> e2 (dynamic_cast<BackdraftEditor*> (p2.createEditor()));
        e2->setVisible (true);
        for (int k = 0; k < 6; ++k) { e2->timerCallback(); pump (20); }
        snap (*e2, "ui_empty.png", 1.0f);
        e2.reset();
    }
    // 150 %
    ed->setSize (1020, 1050);
    for (int k = 0; k < 6; ++k) { ed->timerCallback(); pump (20); }
    snap (*ed, "ui_150.png", 1.0f);

    // potard : images pour le GIF (6 crans, rotation rapide)
    ed->setSize (680, 700);
    for (int k = 0; k < 6; ++k) { ed->timerCallback(); pump (40); }
    auto* prm = p.apvts.getParameter ("length");
    int frame = 0;
    auto grab = [&]
    {
        pump (40);
        auto img = ed->createComponentSnapshot (Rectangle<int> (190, 246, 300, 300), true, 1.0f);
        File out = dir.getChildFile ("knob_" + String (frame++).paddedLeft ('0', 3) + ".png");
        FileOutputStream os (out); PNGImageFormat png; png.writeImageToStream (img, os);
    };
    for (int v : { 4, 4, 3, 2, 1, 0, 0, 0, 1, 2, 3, 4, 5, 5, 5, 3, 0, 0, 0, 5, 5, 5, 4, 4, 4 })
    {
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
        grab();
        for (int k = 0; k < 2; ++k) grab();
    }
    ed.reset();
    return 0;
}
