#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "BackdraftMenu.h"
#include "WindowMinSize.h"
#include "NHKnob.h"

//==============================================================================
//  Fenêtre de Backdraft : repère de travail 680 x 700 unités, ouverte à « 60 % » (544 x 560),
//  jamais plus petite, jusqu'à 150 % (1020 x 1050 au coin, 1360 x 1400 au maximum).
//==============================================================================
namespace BackdraftUi
{
    constexpr float kUW = 680.0f, kUH = 700.0f;            // repère de travail
    constexpr int kW = 544, kH = 560;                      // taille par défaut, annoncée « 60 % »
    constexpr int kMaxW = 1360, kMaxH = 1400;              // 150 %
    const juce::Colour kEmber (0xffff7a26), kEmberHot (0xffffba48), kRec (0xffeb3a28),
                       kGrey (0xff7a7a80), kLight (0xffd6d6da), kTitle (0xffe8e8ec);
}

class BackdraftEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit BackdraftEditor (BackdraftProcessor&);
    ~BackdraftEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { fileHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { fileHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    BackdraftProcessor& proc;

private:
    friend int main();                          // outil de captures (tools/render)
    void timerCallback() override;
    float sc() const { return (float) getWidth() / BackdraftUi::kUW; }
    juce::Rectangle<float> U (float x, float y, float w, float h) const { const float s = sc(); return { x * s, y * s, w * s, h * s }; }
    juce::Font font (int weight, float h) const;     // 0 light, 1 regular, 2 medium, 3 semibold, 4 bold (h en unités)
    float glow() const { return 0.25f * (float) (juce::jlimit (0, 3, proc.glowIdx.load()) + 1); }

    // zones (unités)
    enum Zone { zNone = 0, zPreset, zPrev, zNext, zSave, zMenu, zVox, zInst, zReset, zListen, zLoop, zCapture, zImport, zDrag, zWave,
                zFx0, zFx1, zFx2, zFx3, zFx4, zFx5 };
    Zone zoneAt (juce::Point<float> p) const;
    juce::Rectangle<float> zoneRect (Zone z) const;
    juce::Rectangle<float> fxRect (int i) const;
    void layoutFx();
    float fxX[backdraft::numFx] {}, fxW[backdraft::numFx] {};

    // dessin
    void rebuildBackground();
    void rebuildGlows();
    void drawTopBar (juce::Graphics&);
    void drawDisplay (juce::Graphics&);
    void drawKnobArea (juce::Graphics&);
    void drawEffects (juce::Graphics&);
    void drawSpaced (juce::Graphics&, const juce::String& t, const juce::Font& f, float x, float y, float spacing, juce::Justification j, juce::Colour c) const;
    float spacedWidth (const juce::String& t, const juce::Font& f, float spacing) const;
    juce::Image background, fxGlow[backdraft::numFx], fxGlowOff;
    float bgScale = 0.0f;
    int bgW = 0, bgH = 0, resizeSettle = 1;

    // actions
    void showStatus (const juce::String& s);
    void showMainMenu();
    void showPresetMenu();
    void showParamMidiMenu (const juce::String& paramId, const juce::String& title);
    void prevNextPreset (int dir);
    void savePreset();
    void loadPreset();
    void importDialog();
    void toggleFx (int i);
    void setMode (bool inst);
    void startDragOut();
    void setScaleIndex (int idx);
    juce::PopupMenu::Options menuAt() const { return juce::PopupMenu::Options().withMousePosition(); }

    // anti-gel v2
    void useSoftwareRenderer();
    void trackHostWindow();
    void askRepaint (juce::Rectangle<int> r);
    void* lastPeer = nullptr;
    double lastRendererCheck = 0, lastPaint = 0, pendingSince = 0, lastScaleCheck = 0;
    float lastScaleFactor = 1.0f;

    BackdraftMenu::Fonts fonts;
    juce::Typeface::Ptr light, semi;
    BackdraftMenu::MenuLnf menuLnf;
    std::unique_ptr<juce::Drawable> logo;
    std::unique_ptr<nh::Knob> knob;
    wiga::WindowMinSize hostLock;
    std::unique_ptr<juce::FileChooser> chooser;

    // état affiché (le timer ne redessine que ce qui change)
    juce::String status;
    double statusTime = -10.0;
    float statusAlpha = 0.0f;
    int lastLength = -1, lastMode = -1, lastFx[backdraft::numFx], lastRenderCount = -1, lastGlow = -1, lastCapture = -1;
    bool lastListen = false, lastLoop = false, lastRendering = false, fileHover = false;
    float fxLevel[backdraft::numFx] {};            // fondu de l'allumage des effets
    int lastPlay = -1;
    float recBlink = 0.0f, lastCapLevel = 0.0f;
    std::vector<float> liveWave;                   // forme d'onde pendant la capture
    double lastLiveSec = -1.0;
    Zone hover = zNone, pressZone = zNone;
    bool dragStarted = false;
    juce::Point<float> pressPos;
    int lastMidiAct = 0;
    juce::String lastStatusDrawn, lastPresetDrawn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackdraftEditor)
};
