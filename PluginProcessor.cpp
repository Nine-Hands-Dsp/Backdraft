#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace backdraft;

static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }

//==============================================================================
//  Thread de rendu : recalcule le riser quand la source ou un réglage change
//==============================================================================
class BackdraftProcessor::RenderThread : public Thread
{
public:
    explicit RenderThread (BackdraftProcessor& p) : Thread ("Backdraft render"), proc (p) {}
    ~RenderThread() override { abort = true; signalThreadShouldExit(); notify(); stopThread (4000); }

    void request (const Settings& s)
    {
        {
            const ScopedLock sl (lock);
            wanted = s; pending = true;
        }
        abort = true;            // un rendu en cours s'arrête pour repartir avec les derniers réglages
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (200);
            Settings s;
            {
                const ScopedLock sl (lock);
                if (! pending) continue;
                s = wanted; pending = false;
            }
            abort = false;
            AudioBuffer<float> src;
            {
                const ScopedLock sl (proc.sourceLock);
                if (! proc.sourceReady.load() || proc.source.getNumSamples() < 64) continue;
                src.makeCopyOf (proc.source);
                s.sampleRate = proc.sourceRate;
            }
            proc.rendering = true;
            auto r = render (src, s, abort);
            proc.rendering = false;
            if (r != nullptr && ! abort.load())
            {
                std::shared_ptr<Riser> sp (r.release());
                MessageManager::callAsync ([safe = WeakReference<RenderThread> (this), sp, procPtr = &proc]
                {
                    if (safe.get() == nullptr) return;
                    procPtr->installRiser (sp);
                });
            }
        }
    }

    std::atomic<bool> abort { false };
private:
    BackdraftProcessor& proc;
    CriticalSection lock;
    Settings wanted;
    bool pending = false;
    JUCE_DECLARE_WEAK_REFERENCEABLE (RenderThread)
};

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout BackdraftProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray lengths;
    for (int i = 0; i < numLengths; ++i) lengths.add (lengthName (i).toLowerCase().replace ("bar", "Bar").replace ("beat", "Beat"));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, "Reverse length", lengths, 4));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Vox", "Inst" }, 0));
    for (int i = 0; i < numFx; ++i)
    {
        String n (fxName (i));
        n = n.substring (0, 1) + n.substring (1).toLowerCase();
        l.add (std::make_unique<AudioParameterBool> (ParameterID { fxId (i), 1 }, n, false));
    }
    return l;
}

BackdraftProcessor::BackdraftProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BACKDRAFT", createLayout())
{
    lengthParam = apvts.getRawParameterValue ("length");
    modeParam   = apvts.getRawParameterValue ("mode");
    for (int i = 0; i < numFx; ++i) fxParam[i] = apvts.getRawParameterValue (fxId (i));
    for (int i = 0; i < 128; ++i) { ccMap[i] = -1; ccMin[i] = 127; ccMax[i] = 0; }
    for (int i = 0; i < kMaxParams; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; }
    jassert (getParameters().size() <= kMaxParams);
    formats.registerBasicFormats();
    renderer = std::make_unique<RenderThread> (*this);
    renderer->startThread (Thread::Priority::low);
    lastRequested = currentSettings();
    loadFactoryPreset (0);
    startTimerHz (30);
}

BackdraftProcessor::~BackdraftProcessor()
{
    stopTimer();
    renderer.reset();
}

bool BackdraftProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

Settings BackdraftProcessor::currentSettings() const
{
    Settings s;
    s.lengthIdx = jlimit (0, numLengths - 1, (int) std::round (lengthParam->load()));
    s.instrument = modeParam->load() > 0.5f;
    for (int i = 0; i < numFx; ++i) s.fx[i] = fxParam[i]->load() > 0.5f;
    s.bpm = hostBpm.load();
    s.sampleRate = rate.load();
    return s;
}

void BackdraftProcessor::prepareToPlay (double sr, int block)
{
    const bool rateChanged = std::abs (sr - rate.load()) > 0.5;
    rate = sr;
    maxBlock = block;
    capBuf.setSize (2, (int) (kMaxCaptureSeconds * sr) + block + 16, false, true, true);
    if (rateChanged && sourceReady.load())
    {
        // la source suit le taux du projet (thread de messages, hors audio)
        MessageManager::callAsync ([this, sr] { resampleSourceTo (sr); requestRender(); });
    }
}

//==============================================================================
void BackdraftProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    if (numIn == 1 && numOut > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = jmax (numIn, numIn == 1 ? 2 : 0); c < numOut; ++c) buffer.clear (c, 0, n);

    // tempo du projet
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 20.0 && *bpm < 400.0) hostBpm = *bpm;

    //-------------------------------------------------------------- MIDI : Learn + CC appris (file vers le thread de messages)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController()) continue;
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        const int lp = learnParam.load();
        if (lp >= 0)
        {
            for (auto& x : ccMap) if (x.load() == lp) x = -1;
            ccMap[cc] = lp; learnParam = -1; lastLearnedCc = cc;
            ccMin[cc] = v; ccMax[cc] = v;
        }
        const int pi = ccMap[cc].load();
        if (pi < 0 || pi >= kMaxParams) continue;
        if (v < ccMin[cc].load()) ccMin[cc] = v;
        if (v > ccMax[cc].load()) ccMax[cc] = v;
        const int lo = ccMin[cc].load(), hi = ccMax[cc].load();
        float x = (float) v / 127.0f;
        if (hi - lo >= 64) x = jlimit (0.0f, 1.0f, ((float) (v - lo) - 1.0f) / jmax (1.0f, (float) (hi - lo) - 2.0f));
        pendingValue[pi] = x;
        pendingSet[pi] = true;
        midiActivity.fetch_add (1);
    }
    midi.clear();

    const int chs = jmin (2, buffer.getNumChannels());

    //-------------------------------------------------------------- capture
    if (captureState.load() == 1 && ! captureFull.load())
    {
        int fill = captureFill.load();
        const int room = capBuf.getNumSamples() - fill;
        const int k = jmin (n, room);
        float pk = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            const int sc = jmin (c, chs - 1);
            if (sc < 0) break;
            capBuf.copyFrom (c, fill, buffer, sc, 0, k);
            pk = jmax (pk, buffer.getMagnitude (sc, 0, k));
        }
        captureFill = fill + k;
        capLevel = jmax (pk, capLevel.load() * 0.9f);
        if (k < n || fill + k >= (int) (kMaxCaptureSeconds * rate.load())) captureFull = true;
    }

    //-------------------------------------------------------------- écoute du riser (mélangée au signal de la piste)
    const bool want = listening.load();
    if (want || fade > 0.0f)
    {
        auto r = std::atomic_load (&riserShared);
        if (r != nullptr && r->audio.getNumSamples() > 0)
        {
            const int total = r->audio.getNumSamples();
            int pos = playPos.load();
            const float step = 1.0f / (0.010f * (float) rate.load());
            for (int i = 0; i < n; ++i)
            {
                fade = want ? jmin (1.0f, fade + step) : jmax (0.0f, fade - step);
                if (pos >= total)
                {
                    if (loop.load() && want) pos = 0;
                    else { listening = false; fade = 0.0f; break; }
                }
                const float l = r->audio.getSample (0, pos) * fade, rr = r->audio.getSample (1, pos) * fade;
                if (chs > 0) buffer.addSample (0, i, l);
                if (chs > 1) buffer.addSample (1, i, rr);
                ++pos;
                if (fade <= 0.0f && ! want) break;
            }
            playPos = pos;
        }
        else { listening = false; fade = 0.0f; }
    }
}

//==============================================================================
void BackdraftProcessor::timerCallback()
{
    auto& params = getParameters();
    for (int i = 0; i < params.size() && i < kMaxParams; ++i)
        if (pendingSet[i].exchange (false))
        {
            auto* p = params[i];
            p->beginChangeGesture();
            p->setValueNotifyingHost (pendingValue[i].load());
            p->endChangeGesture();
        }

    // capture pleine : elle devient la source
    if (captureState.load() == 1 && captureFull.load()) stopCapture();
    if (captureState.load() == 1) capLevel = capLevel.load() * 0.85f;

    // réglages qui changent -> nouveau rendu (petit délai pour regrouper les mouvements de potard)
    const auto s = currentSettings();
    if (s != lastRequested)
    {
        // le tempo ne relance un rendu que s'il change vraiment (> 0,5 BPM)
        auto a = s, b = lastRequested; a.bpm = b.bpm = 0;
        if (a != b || std::abs (s.bpm - lastRequested.bpm) > 0.5)
        {
            lastRequested = s;
            renderWanted = true;
            lastSettingsChange = nowSeconds();
            if (a != b && ! presetDirty.load()) presetDirty = true;
        }
    }
    if (renderWanted && nowSeconds() - lastSettingsChange > 0.12) { renderWanted = false; requestRender(); }
}

void BackdraftProcessor::requestRender()
{
    lastRequested = currentSettings();
    if (sourceReady.load() && renderer != nullptr) renderer->request (lastRequested);
}

void BackdraftProcessor::installRiser (std::shared_ptr<Riser> r)
{
    r->presetName = presetName;
    std::shared_ptr<const Riser> sp (std::move (r));
    auto old = std::atomic_exchange (&riserShared, sp);
    // l'ancien riser est libéré plus tard sur le thread de messages, jamais par le thread audio
    if (old != nullptr)
        Timer::callAfterDelay (3000, [old] {});
    if (playPos.load() >= sp->audio.getNumSamples()) playPos = 0;
    renderCount.fetch_add (1);
}

//==============================================================================
//  Capture / import
//==============================================================================
void BackdraftProcessor::startCapture()
{
    stopListen();
    captureFill = 0;
    captureFull = false;
    capLevel = 0.0f;
    captureState = 1;
}

void BackdraftProcessor::stopCapture()
{
    if (captureState.exchange (0) != 1) return;
    const int n = captureFill.load();
    if (n < (int) (0.05 * rate.load())) return;   // prise vide : la source précédente reste
    AudioBuffer<float> b (2, n);
    for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, capBuf, c, 0, n);
    // fin de prise : on retire le silence de fin
    int end = n;
    float pk = jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n));
    const float thr = jmax (pk * 0.003f, 1.0e-5f);
    while (end > 64 && std::abs (b.getSample (0, end - 1)) < thr && std::abs (b.getSample (1, end - 1)) < thr) --end;
    if (pk < 1.0e-5f) return;                      // silence complet
    b.setSize (2, jmin (n, end + (int) (0.05 * rate.load())), true, true, true);
    const auto f = saveCaptureFile (b, rate.load());
    setSourceFromBuffer (b, rate.load(), "Capture " + Time::getCurrentTime().formatted ("%H:%M:%S"));
    { const ScopedLock sl (sourceLock); srcPath = f.getFullPathName(); }
    requestRender();
}

File BackdraftProcessor::userFolder (const String& sub)
{
    auto f = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Nine Hands DSP").getChildFile ("Backdraft").getChildFile (sub);
    f.createDirectory();
    return f;
}

File BackdraftProcessor::saveCaptureFile (const AudioBuffer<float>& b, double sr)
{
    auto f = userFolder ("Captures").getChildFile ("Capture " + Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
    f = f.getNonexistentSibling();
    WavAudioFormat wav;
    if (auto os = std::unique_ptr<OutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
    return f;
}

bool BackdraftProcessor::importFile (const File& f, String& error)
{
    std::unique_ptr<AudioFormatReader> rd (formats.createReaderFor (f));
    if (rd == nullptr) { error = "Unsupported file"; return false; }
    const int64 maxLen = (int64) (kMaxCaptureSeconds * rd->sampleRate);
    const int len = (int) jmin (rd->lengthInSamples, maxLen);
    if (len < 64) { error = "File too short"; return false; }
    AudioBuffer<float> b ((int) jmin (2u, rd->numChannels), len);
    rd->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
    if (b.getNumChannels() == 1) { AudioBuffer<float> st (2, len); st.copyFrom (0, 0, b, 0, 0, len); st.copyFrom (1, 0, b, 0, 0, len); b = std::move (st); }
    stopListen();
    setSourceFromBuffer (b, rd->sampleRate, f.getFileNameWithoutExtension());
    { const ScopedLock sl (sourceLock); srcPath = f.getFullPathName(); }
    requestRender();
    return true;
}

void BackdraftProcessor::setSourceFromBuffer (const AudioBuffer<float>& b, double srcRate, const String& name)
{
    {
        const ScopedLock sl (sourceLock);
        sourceOriginal.makeCopyOf (b);
        sourceOriginalRate = srcRate;
        srcName = name;
    }
    resampleSourceTo (rate.load());
    sourceReady = true;
}

void BackdraftProcessor::resampleSourceTo (double newRate)
{
    const ScopedLock sl (sourceLock);
    if (sourceOriginal.getNumSamples() == 0) return;
    if (std::abs (sourceOriginalRate - newRate) < 0.5)
    {
        source.makeCopyOf (sourceOriginal);
    }
    else
    {
        const double ratio = sourceOriginalRate / newRate;
        const int outLen = (int) std::floor (sourceOriginal.getNumSamples() / ratio) - 4;
        source.setSize (2, jmax (1, outLen));
        for (int c = 0; c < 2; ++c)
        {
            LagrangeInterpolator li;
            li.process (ratio, sourceOriginal.getReadPointer (jmin (c, sourceOriginal.getNumChannels() - 1)), source.getWritePointer (c), source.getNumSamples());
        }
    }
    sourceRate = newRate;
}

File BackdraftProcessor::exportRiserFile()
{
    auto r = currentRiser();
    if (r == nullptr || r->riserLength <= 0) return {};
    // même riser glissé plusieurs fois : le même fichier est réutilisé
    if (lastExportCount == renderCount.load() && lastExportFile.existsAsFile()) return lastExportFile;
    String name = "Backdraft " + presetName + " " + lengthName (r->settings.lengthIdx).toLowerCase() + " "
                + String (roundToInt (r->settings.bpm)) + "bpm " + Time::getCurrentTime().formatted ("%H-%M-%S");
    name = File::createLegalFileName (name);
    auto f = userFolder ("Risers").getChildFile (name + ".wav").getNonexistentSibling();
    WavAudioFormat wav;
    if (auto os = std::unique_ptr<OutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (os.get(), r->sampleRate, 2, 24, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (r->audio, 0, r->riserLength);
            w.reset();
            lastExportCount = renderCount.load();
            lastExportFile = f;
            return f;
        }
    return {};
}

void BackdraftProcessor::toggleListen()
{
    if (listening.load()) { listening = false; return; }
    if (currentRiser() == nullptr) return;
    playPos = 0;
    listening = true;
}

//==============================================================================
//  MIDI Learn
//==============================================================================
int BackdraftProcessor::paramIndexOf (const String& id) const
{
    auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (params[i]))
            if (wp->paramID == id) return i;
    return -1;
}

int BackdraftProcessor::ccForParam (int pi) const
{
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() == pi) return c;
    return -1;
}

void BackdraftProcessor::clearMidiFor (int pi)
{
    for (auto& x : ccMap) if (x.load() == pi) x = -1;
    if (learnParam.load() == pi) learnParam = -1;
}

//==============================================================================
//  Presets d'usine (thème du feu : Backdraft = l'appel d'air avant l'embrasement)
//==============================================================================
const std::vector<BackdraftProcessor::FactoryPreset>& BackdraftProcessor::factoryPresets()
{
    //                                  len  inst   PHONE  LO-FI  CHORUS REVERB DRIVE  TREMOLO
    static const std::vector<FactoryPreset> p = {
        { "Ignition",       4, false, { false, false, false, false, false, false } },
        { "Slow Burn",      5, false, { false, false, true,  false, false, false } },
        { "Afterglow",      4, false, { false, false, true,  true,  false, false } },
        { "Smoke Signal",   3, false, { true,  false, false, false, false, false } },
        { "Ember Tape",     4, false, { false, true,  false, false, false, false } },
        { "Flashover",      3, false, { false, false, false, false, true,  true  } },
        { "Ghost Call",     4, false, { true,  false, false, false, false, true  } },
        { "Halo",           5, false, { false, false, false, true,  false, false } },
        { "Wildfire",       4, false, { false, false, true,  true,  true,  true  } },
        { "Quick Spark",    1, false, { false, false, false, false, false, false } },
        { "Inst Swell",     4, true,  { false, false, false, false, false, false } },
        { "Inst Cathedral", 5, true,  { false, false, true,  true,  false, false } },
        { "Inst Furnace",   3, true,  { false, false, false, false, true,  false } },
    };
    return p;
}

int BackdraftProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const String BackdraftProcessor::getProgramName (int i) { return isPositiveAndBelow (i, (int) factoryPresets().size()) ? String (factoryPresets()[(size_t) i].name) : String(); }
void BackdraftProcessor::setCurrentProgram (int i) { if (isPositiveAndBelow (i, getNumPrograms())) loadFactoryPreset (i); }

static void setParam (AudioProcessorValueTreeState& s, const char* id, float plain)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void BackdraftProcessor::loadFactoryPreset (int index)
{
    const auto& fp = factoryPresets()[(size_t) jlimit (0, (int) factoryPresets().size() - 1, index)];
    setParam (apvts, "length", (float) fp.length);
    setParam (apvts, "mode", fp.inst ? 1.0f : 0.0f);
    for (int i = 0; i < numFx; ++i) setParam (apvts, fxId (i), fp.fx[i] ? 1.0f : 0.0f);
    currentPreset = index;
    presetName = fp.name;
    lastRequested = currentSettings();
    presetDirty = false;
    requestRender();
}

void BackdraftProcessor::resetToDefault() { loadFactoryPreset (0); }

ValueTree BackdraftProcessor::savePresetTree() const
{
    ValueTree t ("BACKDRAFT_PRESET");
    t.setProperty ("length", lengthParam->load(), nullptr);
    t.setProperty ("mode", modeParam->load(), nullptr);
    for (int i = 0; i < numFx; ++i) t.setProperty (fxId (i), fxParam[i]->load(), nullptr);
    return t;
}

void BackdraftProcessor::loadPresetTree (const ValueTree& t, const String& name)
{
    if (! t.hasType ("BACKDRAFT_PRESET")) return;
    setParam (apvts, "length", (float) t.getProperty ("length", 4));
    setParam (apvts, "mode", (float) t.getProperty ("mode", 0));
    for (int i = 0; i < numFx; ++i) setParam (apvts, fxId (i), (float) t.getProperty (fxId (i), 0));
    presetName = name;
    currentPreset = -1;
    lastRequested = currentSettings();
    presetDirty = false;
    requestRender();
}

//==============================================================================
void BackdraftProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiSize", uiSizeIdx.load(), nullptr);
    state.setProperty ("glow", glowIdx.load(), nullptr);
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("presetDirty", presetDirty.load(), nullptr);
    state.setProperty ("loop", loop.load(), nullptr);
    {
        const ScopedLock sl (sourceLock);
        state.setProperty ("sourcePath", srcPath, nullptr);
        state.setProperty ("sourceName", srcName, nullptr);
    }
    String map;
    for (int c = 0; c < 128; ++c)
        if (ccMap[c].load() >= 0)
            if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (getParameters()[ccMap[c].load()]))
                map << c << "=" << wp->paramID << ";";
    state.setProperty ("midiMap", map, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void BackdraftProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto t = ValueTree::fromXml (*xml);
        if (! t.isValid()) return;
        apvts.replaceState (t);
        uiSizeIdx     = (int) t.getProperty ("uiSize", -1);
        glowIdx       = jlimit (0, 3, (int) t.getProperty ("glow", 2));
        currentPreset = (int) t.getProperty ("preset", 0);
        presetName    = t.getProperty ("presetName", "Ignition").toString();
        presetDirty   = (bool) t.getProperty ("presetDirty", false);
        loop          = (bool) t.getProperty ("loop", false);
        for (auto& x : ccMap) x = -1;
        for (auto& e : StringArray::fromTokens (t.getProperty ("midiMap").toString(), ";", ""))
        {
            const int c = e.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const int pi = paramIndexOf (e.fromFirstOccurrenceOf ("=", false, false));
            if (isPositiveAndBelow (c, 128) && pi >= 0) ccMap[c] = pi;
        }
        // la source (capture ou fichier) est rechargée depuis son fichier
        const File f (t.getProperty ("sourcePath").toString());
        if (f.existsAsFile())
        {
            String err;
            const auto nm = t.getProperty ("sourceName").toString();
            if (importFile (f, err) && nm.isNotEmpty()) { const ScopedLock sl (sourceLock); srcName = nm; }
        }
        lastRequested = currentSettings();
        requestRender();
    }
}

AudioProcessorEditor* BackdraftProcessor::createEditor() { return new BackdraftEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BackdraftProcessor(); }
