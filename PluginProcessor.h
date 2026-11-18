#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <vector>
#include "RiserEngine.h"

//==============================================================================
//  Backdraft (NINE HANDS | DSP) : riser « reverse reverb » en un clic, inspiré d'AtmosRiser (WaveAtmos)
//  - CAPTURE : enregistre ce qui passe sur la piste (30 s au plus) ; IMPORT : charge un fichier audio
//  - le riser est recalculé à chaque réglage (thread de rendu), écouté avec LISTEN / LOOP
//  - DRAG : le riser est glissé de la fenêtre vers la timeline du DAW (fichier WAV)
//  Le signal de la piste traverse le plugin sans modification.
//==============================================================================
class BackdraftProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BackdraftProcessor();
    ~BackdraftProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //------------------------------------------------------------------ paramètres
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* lengthParam = nullptr, *modeParam = nullptr, *fxParam[backdraft::numFx] {};
    backdraft::Settings currentSettings() const;

    //------------------------------------------------------------------ source (capture ou fichier)
    enum class CaptureState { idle, recording };
    std::atomic<int> captureState { 0 };            // 0 au repos, 1 en enregistrement
    std::atomic<int> captureFill { 0 };             // échantillons enregistrés
    static constexpr double kMaxCaptureSeconds = 30.0;
    void startCapture();                            // thread de messages
    void stopCapture();                             // thread de messages : la prise devient la source
    float captureLevel() const { return capLevel.load(); }
    double captureSeconds() const { return (double) captureFill.load() / juce::jmax (1.0, rate.load()); }

    bool importFile (const juce::File& f, juce::String& error);   // thread de messages
    bool hasSource() const { return sourceReady.load(); }
    juce::String sourceName() const { const juce::ScopedLock sl (sourceLock); return srcName; }

    //------------------------------------------------------------------ riser rendu (lu sans verrou par l'audio et l'interface)
    std::shared_ptr<const backdraft::Riser> currentRiser() const { return std::atomic_load (&riserShared); }
    std::atomic<bool> rendering { false };
    std::atomic<int> renderCount { 0 };             // incrémenté à chaque nouveau riser
    void requestRender();                           // thread de messages
    juce::File exportRiserFile();                   // WAV 24 bits pour le glisser-déposer (thread de messages)

    //------------------------------------------------------------------ écoute
    std::atomic<bool> listening { false }, loop { false };
    std::atomic<int> playPos { 0 };                 // position de lecture (échantillons, dans le riser + phrase)
    void toggleListen();
    void stopListen() { listening = false; }

    //------------------------------------------------------------------ tempo du projet
    std::atomic<double> hostBpm { 120.0 }, rate { 48000.0 };

    //------------------------------------------------------------------ réglages de l'interface (enregistrés avec le projet)
    std::atomic<int> uiSizeIdx { -1 };      // -1 = taille par défaut (60 %)
    std::atomic<int> glowIdx { 2 };         // 25 / 50 / 75 / 100 %
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> presetDirty { false };
    juce::String presetName { "Ignition" };

    //------------------------------------------------------------------ presets
    struct FactoryPreset { const char* name; int length; bool inst; bool fx[backdraft::numFx]; };
    static const std::vector<FactoryPreset>& factoryPresets();
    void loadFactoryPreset (int index);
    void resetToDefault();
    juce::ValueTree savePresetTree() const;
    void loadPresetTree (const juce::ValueTree& t, const juce::String& name);

    //------------------------------------------------------------------ MIDI Learn (aucun CC par défaut)
    static constexpr int kMaxParams = 16;
    std::atomic<int> learnParam { -1 };
    std::atomic<int> ccMap[128];
    std::atomic<int> lastLearnedCc { -1 };
    int  paramIndexOf (const juce::String& id) const;
    int  ccForParam (int paramIndex) const;
    void clearMidiFor (int paramIndex);
    std::atomic<int> midiActivity { 0 };

    static juce::File userFolder (const juce::String& sub);   // Documents/Nine Hands DSP/Backdraft/<sub>

private:
    void timerCallback() override;          // file MIDI -> setValueNotifyingHost, rendus, fin de capture
    std::atomic<float> pendingValue[kMaxParams];
    std::atomic<bool>  pendingSet[kMaxParams];
    std::atomic<int>   ccMin[128], ccMax[128];

    // capture (tampon préalloué, écrit par le thread audio)
    juce::AudioBuffer<float> capBuf;
    std::atomic<float> capLevel { 0.0f };
    std::atomic<bool> captureFull { false };

    // source
    juce::CriticalSection sourceLock;                 // jamais pris par le thread audio
    juce::AudioBuffer<float> source;                  // au taux du projet
    double sourceRate = 48000.0;
    juce::AudioBuffer<float> sourceOriginal;          // fichier tel que chargé (rééchantillonné si le taux change)
    double sourceOriginalRate = 48000.0;
    juce::String srcName, srcPath;
    std::atomic<bool> sourceReady { false };
    void setSourceFromBuffer (const juce::AudioBuffer<float>& b, double srcRate, const juce::String& name);
    void resampleSourceTo (double newRate);
    juce::File saveCaptureFile (const juce::AudioBuffer<float>& b, double sr);

    // rendu
    class RenderThread;
    std::unique_ptr<RenderThread> renderer;
    std::shared_ptr<const backdraft::Riser> riserShared;
    backdraft::Settings lastRequested;
    bool renderWanted = false;
    double lastSettingsChange = 0.0;
    void installRiser (std::shared_ptr<backdraft::Riser> r);
    friend class RenderThread;

    juce::AudioFormatManager formats;
    int lastExportCount = -1;
    juce::File lastExportFile;
    float fade = 0.0f;                         // fondu d'entrée / sortie de l'écoute
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackdraftProcessor)
};
