#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include <atomic>
#include <random>

//==============================================================================
//  Backdraft (NINE HANDS | DSP) : moteur de rendu du riser « reverse reverb »
//
//  Principe (méthode manuelle des studios, en un seul calcul) :
//    1. début de la phrase (attaque) préparé : filtre d'entrée, anti-sifflantes automatique (VOX) ;
//    2. ce début est retourné, puis envoyé dans une grande reverb (FDN 8 lignes, 100 % wet) ;
//    3. la queue de la reverb est retournée à son tour : elle monte et finit exactement
//       là où la phrase commence ;
//    4. effets en un clic sur la montée (PHONE, LO-FI, CHORUS, SHIMMER, DRIVE, TREMOLO),
//       courbe de montée, normalisation.
//  Tout est calculé hors du thread audio (thread de rendu du processeur).
//==============================================================================
namespace backdraft
{
    enum Fx { fxPhone = 0, fxLofi, fxChorus, fxShimmer, fxDrive, fxTremolo, numFx };
    inline const char* fxName (int i) { static const char* n[numFx] = { "PHONE", "LO-FI", "CHORUS", "SHIMMER", "DRIVE", "TREMOLO" }; return n[i]; }
    inline const char* fxId (int i)   { static const char* n[numFx] = { "phone", "lofi", "chorus", "shimmer", "drive", "tremolo" }; return n[i]; }

    // durées proposées par le potard (en temps)
    constexpr int numLengths = 6;
    inline double lengthBeats (int i) { static const double b[numLengths] = { 1, 2, 4, 8, 16, 32 }; return b[juce::jlimit (0, numLengths - 1, i)]; }
    inline juce::String lengthName (int i)
    {
        static const char* n[numLengths] = { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
        return n[juce::jlimit (0, numLengths - 1, i)];
    }

    struct Settings
    {
        int lengthIdx = 4;
        bool instrument = false;        // VOX (false) / INST (true)
        bool fx[numFx] {};
        double bpm = 120.0;
        double sampleRate = 48000.0;
        bool operator== (const Settings& o) const
        {
            if (lengthIdx != o.lengthIdx || instrument != o.instrument || std::abs (bpm - o.bpm) > 0.01 || sampleRate != o.sampleRate) return false;
            for (int i = 0; i < numFx; ++i) if (fx[i] != o.fx[i]) return false;
            return true;
        }
        bool operator!= (const Settings& o) const { return ! (*this == o); }
    };

    //------------------------------------------------------------------ filtres de base (biquad RBJ, double précision)
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        inline double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
        { b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0; }
        void lowPass (double sr, double f, double q = 0.7071)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, f) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
            set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
        }
        void highPass (double sr, double f, double q = 0.7071)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, f) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
            set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
        }
        void peak (double sr, double f, double q, double db)
        {
            const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, f) / sr;
            const double c = std::cos (w), al = std::sin (w) / (2 * q);
            set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
        }
        void highShelf (double sr, double f, double db)
        {
            const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, f) / sr;
            const double c = std::cos (w), s = std::sin (w), al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
            set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                 (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        }
    };

    //------------------------------------------------------------------ reverb FDN 8 lignes (Householder), diffusion d'entrée
    class Fdn
    {
    public:
        void prepare (double sr, double rt60, double size, double damp)
        {
            fs = sr;
            static const double base[8] = { 0.0297, 0.0371, 0.0411, 0.0437, 0.0533, 0.0613, 0.0677, 0.0749 };
            for (int i = 0; i < 8; ++i)
            {
                len[i] = juce::jmax (8, (int) std::round (base[i] * size * sr));
                line[i].assign ((size_t) len[i] + 4, 0.0);
                pos[i] = 0;
                const double d = (double) len[i] / sr;
                g[i] = std::pow (10.0, -3.0 * d / rt60);
                lp[i] = 0.0;
                // amortissement des aigus : plus la ligne est longue, plus elle perd d'aigus
                dampK[i] = juce::jlimit (0.0, 0.95, damp * (0.6 + 0.4 * d / 0.075));
            }
            static const double ap[4] = { 0.0047, 0.0036, 0.0127, 0.0093 };
            for (int s = 0; s < 2; ++s)
                for (int i = 0; i < 4; ++i)
                {
                    apLen[s][i] = juce::jmax (4, (int) std::round (ap[i] * (s == 0 ? 1.0 : 1.11) * sr));
                    apBuf[s][i].assign ((size_t) apLen[s][i], 0.0);
                    apPos[s][i] = 0;
                }
            // modulation lente des lignes (évite le métal)
            modPhase = 0.0;
        }

        // x = entrée (L, R) -> y = sortie wet (L, R)
        inline void process (double xl, double xr, double& yl, double& yr)
        {
            xl = diffuse (0, xl);
            xr = diffuse (1, xr);
            double o[8];
            for (int i = 0; i < 8; ++i)
            {
                // lecture avec petite modulation sur 2 lignes (interpolation linéaire)
                double rd = (double) pos[i] - (double) len[i];
                if (i == 2 || i == 5)
                    rd -= 1.5 + 1.5 * std::sin (modPhase + i);
                while (rd < 0) rd += (double) line[i].size();
                const int i0 = (int) rd; const double fr = rd - i0;
                const int i1 = (i0 + 1) % (int) line[i].size();
                double v = line[i][(size_t) i0] * (1 - fr) + line[i][(size_t) i1] * fr;
                lp[i] = v + dampK[i] * (lp[i] - v);
                o[i] = lp[i] * g[i];
            }
            modPhase += juce::MathConstants<double>::twoPi * 0.35 / fs;
            if (modPhase > juce::MathConstants<double>::twoPi * 64.0) modPhase -= juce::MathConstants<double>::twoPi * 64.0;
            // matrice de Householder : y = o - 2/N * sum(o)
            double sum = 0; for (double v : o) sum += v;
            const double k = sum * 0.25;
            for (int i = 0; i < 8; ++i)
            {
                const double in = (i & 1) ? xr : xl;
                line[i][(size_t) pos[i]] = o[i] - k + in * 0.35;
                pos[i] = (pos[i] + 1) % (int) line[i].size();
            }
            yl = (o[0] - o[2] + o[4] - o[6] + 0.5 * (o[1] + o[5])) * 0.5;
            yr = (o[1] - o[3] + o[5] - o[7] + 0.5 * (o[0] + o[4])) * 0.5;
        }

    private:
        inline double diffuse (int s, double x)
        {
            for (int i = 0; i < 4; ++i)
            {
                auto& b = apBuf[s][i]; int& p = apPos[s][i];
                const double d = b[(size_t) p];
                const double y = -0.62 * x + d;
                b[(size_t) p] = x + 0.62 * y;
                p = (p + 1) % apLen[s][i];
                x = y;
            }
            return x;
        }
        double fs = 48000;
        std::vector<double> line[8];
        int len[8] {}, pos[8] {};
        double g[8] {}, lp[8] {}, dampK[8] {};
        std::vector<double> apBuf[2][4];
        int apLen[2][4] {}, apPos[2][4] {};
        double modPhase = 0;
    };

    //------------------------------------------------------------------ transposition par grains (octave au-dessus) pour le SHIMMER
    inline void pitchUpOctave (const std::vector<float>& in, std::vector<float>& out, double sr)
    {
        const int n = (int) in.size();
        out.assign ((size_t) n, 0.0f);
        const int grain = juce::jmax (64, (int) (0.070 * sr));
        // deux têtes de lecture décalées d'un demi-grain, fenêtres en cosinus (somme constante)
        for (int head = 0; head < 2; ++head)
        {
            for (int start = -(head * grain / 2); start < n; start += grain)
            {
                for (int k = 0; k < grain; ++k)
                {
                    const int o = start + k;
                    if (o < 0 || o >= n) continue;
                    const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * k / grain);
                    const double src = (double) start + 2.0 * k;                   // lecture à double vitesse
                    const int s0 = (int) src;
                    if (s0 < 0 || s0 + 1 >= n) continue;
                    const double fr = src - s0;
                    out[(size_t) o] += (float) (w * (in[(size_t) s0] * (1 - fr) + in[(size_t) s0 + 1] * fr));
                }
            }
        }
    }

    //------------------------------------------------------------------ résultat d'un rendu
    struct Riser
    {
        juce::AudioBuffer<float> audio;     // stéréo, au taux du projet
        int riserLength = 0;                // échantillons de la montée (avant la phrase)
        int phraseLength = 0;               // échantillons de la phrase qui suit (écoute LISTEN)
        double sampleRate = 48000;
        std::vector<float> peaks;           // 96 barres (enveloppe de la montée) pour l'écran
        Settings settings;
        juce::String presetName;
    };

    //------------------------------------------------------------------ rendu complet (thread de rendu)
    // src : audio source (stéréo ou mono) au taux sr ; abort : arrêt anticipé si un nouveau rendu est demandé
    inline std::unique_ptr<Riser> render (const juce::AudioBuffer<float>& source, const Settings& st, const std::atomic<bool>& abort)
    {
        const double sr = st.sampleRate;
        const int srcCh = source.getNumChannels();
        const int srcLen = source.getNumSamples();
        if (srcLen < 64 || srcCh <= 0) return nullptr;

        // 1. début de la phrase : on saute le silence d'entrée (-48 dBFS)
        int startIdx = 0;
        {
            float peak = 0.0f;
            for (int c = 0; c < srcCh; ++c) peak = juce::jmax (peak, source.getMagnitude (c, 0, srcLen));
            const float thr = juce::jmax (peak * 0.004f, 1.0e-5f);
            for (; startIdx < srcLen; ++startIdx)
            {
                float a = 0.0f;
                for (int c = 0; c < srcCh; ++c) a = juce::jmax (a, std::abs (source.getSample (c, startIdx)));
                if (a > thr) break;
            }
            startIdx = juce::jmax (0, startIdx - (int) (0.004 * sr));
            if (startIdx >= srcLen - 64) startIdx = 0;
        }

        const double beats = lengthBeats (st.lengthIdx);
        const double bpm = juce::jlimit (30.0, 300.0, st.bpm);
        const int L = juce::jmax (256, (int) std::round (beats * 60.0 / bpm * sr));     // durée de la montée
        const int H = juce::jmin (srcLen - startIdx, (int) (juce::jlimit (0.35, 1.6, beats * 60.0 / bpm * 0.35) * sr)); // attaque utilisée
        const int phraseLen = juce::jmin (srcLen - startIdx, (int) (4.0 * sr));

        // 2. préparation de l'attaque (mono -> stéréo), filtre d'entrée, anti-sifflantes
        std::vector<float> head[2];
        for (int c = 0; c < 2; ++c)
        {
            head[c].assign ((size_t) H, 0.0f);
            const int sc = juce::jmin (c, srcCh - 1);
            const float* s = source.getReadPointer (sc, startIdx);
            for (int i = 0; i < H; ++i) head[c][(size_t) i] = s[i];
        }
        {
            Biquad hp[2], hp2[2], tilt[2];
            for (int c = 0; c < 2; ++c)
            {
                hp[c].highPass (sr, st.instrument ? 60.0 : 170.0, 0.7071);
                hp2[c].highPass (sr, st.instrument ? 60.0 : 170.0, 0.7071);
                tilt[c].peak (sr, st.instrument ? 2500.0 : 3200.0, 0.8, st.instrument ? 0.0 : 1.5);
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < H; ++i)
                {
                    double x = head[c][(size_t) i];
                    x = tilt[c].process (hp2[c].process (hp[c].process (x)));
                    head[c][(size_t) i] = (float) x;
                }
        }
        if (! st.instrument)
        {
            // anti-sifflantes automatique : détection de l'énergie 5-10 kHz par rapport au signal complet,
            // la bande haute est atténuée seulement pendant les « S » (rien n'est terne le reste du temps)
            Biquad dl[2], dh[2], sl, sh2;
            sl.highPass (sr, 4800.0, 0.7071); sh2.highPass (sr, 4800.0, 0.7071);
            double envHi = 0, envAll = 0;
            const double att = std::exp (-1.0 / (0.002 * sr)), rel = std::exp (-1.0 / (0.060 * sr));
            for (int c = 0; c < 2; ++c) { dl[c].lowPass (sr, 4800.0, 0.5); dh[c].highPass (sr, 4800.0, 0.5); }
            double gSm = 1.0;
            for (int i = 0; i < H; ++i)
            {
                const double m = 0.5 * (head[0][(size_t) i] + head[1][(size_t) i]);
                const double hi = std::abs (sh2.process (sl.process (m)));
                const double al = std::abs (m);
                envHi = hi > envHi ? att * envHi + (1 - att) * hi : rel * envHi + (1 - rel) * hi;
                envAll = al > envAll ? att * envAll + (1 - att) * al : rel * envAll + (1 - rel) * al;
                const double ratio = envHi / (envAll + 1.0e-9);
                // au-delà de 35 % d'énergie dans les aigus : c'est une sifflante
                const double target = ratio > 0.35 ? juce::jlimit (0.08, 1.0, std::pow (0.35 / ratio, 2.2)) : 1.0;
                gSm = target < gSm ? target : gSm + (target - gSm) * 0.002;
                for (int c = 0; c < 2; ++c)
                {
                    const double x = head[c][(size_t) i];
                    const double lo = dl[c].process (x);
                    const double hiB = dh[c].process (x);
                    head[c][(size_t) i] = (float) (lo + hiB * gSm);
                }
            }
        }
        if (abort.load()) return nullptr;

        // fondu de sortie de l'attaque (évite un clic quand la reverb est retournée)
        const int fo = juce::jmin (H / 4, (int) (0.030 * sr));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < fo; ++i)
                head[c][(size_t) (H - 1 - i)] *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / juce::jmax (1, fo)));

        // 3. attaque retournée + shimmer (octave dans la reverb)
        std::vector<float> rev[2];
        for (int c = 0; c < 2; ++c) { rev[c].assign (head[c].rbegin(), head[c].rend()); }
        if (st.fx[fxShimmer])
        {
            for (int c = 0; c < 2; ++c)
            {
                std::vector<float> up;
                pitchUpOctave (rev[c], up, sr);
                for (size_t i = 0; i < rev[c].size(); ++i) rev[c][i] = rev[c][i] * 0.8f + up[i] * 0.75f;
            }
        }
        if (abort.load()) return nullptr;

        // 4. reverb 100 % wet ; on garde la queue (L échantillons) après l'attaque retournée
        const double seconds = (double) L / sr;
        const double rt60 = juce::jlimit (2.5, 40.0, seconds * 2.0 + 1.2);    // la queue garde du corps sur toute la montée
        Fdn fdn;
        fdn.prepare (sr, rt60, st.instrument ? 1.15 : 1.35, st.instrument ? 0.25 : 0.42);
        const int total = H + L;
        std::vector<float> wet[2] { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
        // pré-delay : la montée finit un poil avant l'attaque, pour qu'elle « aspire » la phrase
        const int pre = (int) (0.012 * sr);
        for (int i = 0; i < total; ++i)
        {
            const int k = i - pre;
            const double xl = (k >= 0 && k < H) ? rev[0][(size_t) k] : 0.0;
            const double xr = (k >= 0 && k < H) ? rev[1][(size_t) k] : 0.0;
            double yl, yr;
            fdn.process (xl, xr, yl, yr);
            wet[0][(size_t) i] = (float) yl;
            wet[1][(size_t) i] = (float) yr;
            if ((i & 0x7fff) == 0 && abort.load()) return nullptr;
        }

        // 5. retour à l'endroit : la montée = queue de la reverb, la fin colle au début de la phrase
        auto out = std::make_unique<Riser>();
        out->sampleRate = sr;
        out->settings = st;
        out->riserLength = L;
        out->phraseLength = phraseLen;
        out->audio.setSize (2, L + phraseLen);
        out->audio.clear();
        for (int c = 0; c < 2; ++c)
        {
            float* d = out->audio.getWritePointer (c);
            for (int i = 0; i < L; ++i)
            {
                // i = 0 : début de la montée (fin de la queue) ; i = L-1 : juste avant la phrase
                const int src = H + (L - 1 - i);
                d[i] = src < total ? wet[c][(size_t) src] : 0.0f;
            }
        }

        // 6. effets en un clic sur la montée (t = 0 au début, 1 à la fin)
        float* chL = out->audio.getWritePointer (0);
        float* chR = out->audio.getWritePointer (1);
        const double invL = 1.0 / juce::jmax (1, L - 1);

        if (st.fx[fxPhone])
        {
            Biquad h1[2], h2[2], l1[2], l2[2], pk[2];
            for (int c = 0; c < 2; ++c)
            {
                h1[c].highPass (sr, 420.0, 0.9); h2[c].highPass (sr, 420.0, 0.6);
                l1[c].lowPass (sr, 3100.0, 0.9); l2[c].lowPass (sr, 3100.0, 0.6);
                pk[c].peak (sr, 1500.0, 1.1, 5.0);
            }
            for (int i = 0; i < L; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    float* d = c == 0 ? chL : chR;
                    double x = pk[c].process (l2[c].process (l1[c].process (h2[c].process (h1[c].process (d[i])))));
                    d[i] = (float) (std::tanh (x * 2.2) / 2.2 * 1.6);
                }
        }
        if (st.fx[fxLofi])
        {
            const double hold = sr / 11025.0;
            double acc = hold, hl = 0, hr = 0;
            Biquad lp[2];
            for (auto& f : lp) f.lowPass (sr, 5200.0, 0.7071);
            const double q = 1.0 / 256.0;        // 9 bits
            std::mt19937 rng (17);
            std::uniform_real_distribution<double> dist (-0.5, 0.5);
            for (int i = 0; i < L; ++i)
            {
                acc += 1.0;
                if (acc >= hold) { acc -= hold; hl = chL[i]; hr = chR[i]; }
                double l = std::round (hl / q + dist (rng) * 0.6) * q, r = std::round (hr / q + dist (rng) * 0.6) * q;
                chL[i] = (float) lp[0].process (l);
                chR[i] = (float) lp[1].process (r);
            }
        }
        if (st.fx[fxChorus])
        {
            const int maxD = (int) (0.040 * sr) + 4;
            std::vector<float> dl ((size_t) maxD, 0.0f), dr ((size_t) maxD, 0.0f);
            int wp = 0;
            double ph = 0.0;
            const double rate = juce::MathConstants<double>::twoPi * 0.55 / sr;
            auto readAt = [&] (const std::vector<float>& b, double delay)
            {
                double rp = (double) wp - delay;
                while (rp < 0) rp += maxD;
                const int i0 = (int) rp; const double fr = rp - i0;
                return b[(size_t) i0] * (1 - fr) + b[(size_t) ((i0 + 1) % maxD)] * fr;
            };
            for (int i = 0; i < L; ++i)
            {
                dl[(size_t) wp] = chL[i]; dr[(size_t) wp] = chR[i];
                const double base = 0.016 * sr, depth = 0.0045 * sr;
                const double v1l = readAt (dl, base + depth * std::sin (ph));
                const double v2l = readAt (dl, base * 1.3 + depth * std::sin (ph + 2.1));
                const double v1r = readAt (dr, base + depth * std::sin (ph + juce::MathConstants<double>::pi));
                const double v2r = readAt (dr, base * 1.3 + depth * std::sin (ph + 2.1 + juce::MathConstants<double>::pi));
                chL[i] = (float) (0.62 * chL[i] + 0.5 * (v1l + v2r) * 0.62);
                chR[i] = (float) (0.62 * chR[i] + 0.5 * (v1r + v2l) * 0.62);
                ph += rate;
                wp = (wp + 1) % maxD;
            }
        }
        if (st.fx[fxDrive])
        {
            Biquad pre2[2], post[2];
            for (int c = 0; c < 2; ++c) { pre2[c].peak (sr, 900.0, 0.7, 4.0); post[c].lowPass (sr, 7500.0, 0.7071); }
            // normalisation provisoire pour que la saturation soit indépendante du niveau d'entrée
            float pk = 1.0e-6f;
            for (int i = 0; i < L; ++i) pk = juce::jmax (pk, std::abs (chL[i]), std::abs (chR[i]));
            const double norm = 0.9 / pk;
            for (int i = 0; i < L; ++i)
            {
                const double t = (double) i * invL;
                const double drive = 1.5 + 7.0 * t * t;                 // la saturation monte avec le riser
                for (int c = 0; c < 2; ++c)
                {
                    float* d = c == 0 ? chL : chR;
                    const double x = pre2[c].process (d[i] * norm) * drive;
                    const double y = (std::tanh (x + 0.08) - std::tanh (0.08)) / std::tanh (drive);
                    d[i] = (float) post[c].process (y);
                }
            }
        }
        if (st.fx[fxTremolo])
        {
            // tremolo qui accélère (3 Hz -> 15 Hz), profondeur qui monte aussi
            double ph = 0.0;
            for (int i = 0; i < L; ++i)
            {
                const double t = (double) i * invL;
                const double rate = 3.0 + 12.0 * t * t;
                ph += juce::MathConstants<double>::twoPi * rate / sr;
                const double depth = 0.45 + 0.4 * t;
                const double gn = 1.0 - depth * (0.5 + 0.5 * std::sin (ph));
                chL[i] = (float) (chL[i] * gn);
                chR[i] = (float) (chR[i] * gn);
            }
        }
        if (abort.load()) return nullptr;

        // 7. courbe de montée : départ doux (fondu de 15 % au début), fin franche à pleine intensité,
        //    micro-fondu de 3 ms pour éviter tout clic avant la phrase
        const int fi = juce::jmax (1, (int) (L * 0.15));
        const int fe = (int) (0.003 * sr);
        for (int i = 0; i < L; ++i)
        {
            double gn = 1.0;
            if (i < fi) { const double x = (double) i / fi; gn = x * x * (3 - 2 * x); }
            if (i > L - fe) gn *= (double) (L - i) / fe;
            chL[i] = (float) (chL[i] * gn);
            chR[i] = (float) (chR[i] * gn);
        }

        // 8. normalisation de la montée à -1 dBFS crête
        {
            float pk = 1.0e-9f;
            for (int i = 0; i < L; ++i) pk = juce::jmax (pk, std::abs (chL[i]), std::abs (chR[i]));
            const float gn = 0.891f / pk;
            for (int i = 0; i < L; ++i) { chL[i] *= gn; chR[i] *= gn; }
        }

        // 9. phrase d'origine à la suite (écoute seulement, jamais exportée)
        for (int c = 0; c < 2; ++c)
        {
            const float* s = source.getReadPointer (juce::jmin (c, srcCh - 1), startIdx);
            float* d = out->audio.getWritePointer (c, L);
            for (int i = 0; i < phraseLen; ++i) d[i] = s[i];
        }

        // 10. enveloppe pour l'écran (crête RMS par barre)
        const int bars = 96;
        out->peaks.assign ((size_t) bars, 0.0f);
        float mx = 1.0e-9f;
        for (int b = 0; b < bars; ++b)
        {
            const int a = (int) ((int64_t) b * L / bars), e = (int) ((int64_t) (b + 1) * L / bars);
            double sum = 0; float pk = 0;
            for (int i = a; i < e; ++i) { const float v = 0.5f * (std::abs (chL[i]) + std::abs (chR[i])); sum += (double) v * v; pk = juce::jmax (pk, v); }
            const float rms = (float) std::sqrt (sum / juce::jmax (1, e - a));
            out->peaks[(size_t) b] = 0.55f * rms * 1.41f + 0.45f * pk;
            mx = juce::jmax (mx, out->peaks[(size_t) b]);
        }
        for (auto& p : out->peaks) p /= mx;
        return out;
    }
}
