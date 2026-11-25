#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Shared/CustomLookAndFeel.h"
#include "UI/Shared/VirusComponents.h"
#include "UI/BrowserPanel.h"
#include "UI/Controls.h"
#include "UI/KickDisplays.h"
#include "UI/DragAudioHandle.h"
#include "Browser/LevelReferences.h"

//==============================================================================
/**
    Everything is laid out once at the base size (kBaseWidth x kBaseHeight) inside
    'content', and the editor scales that component with an AffineTransform. All
    drawing is vector based, so it stays sharp at every size.
*/
class KickDrumAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1180, kBaseHeight = 830;

    explicit KickDrumAudioProcessorEditor (KickDrumAudioProcessor&);
    ~KickDrumAudioProcessorEditor() override;

    void resized() override;

private:
    //==============================================================================
    /** The controls of one band page: big display, level strip, mini browser, envelope and level. */
    struct BandControls
    {
        BandControls (KickDrumAudioProcessor&, int band);

        int band;
        kd::ui::BandView view;
        kd::ui::BandLevelStrip levelStrip;
        LcdDisplay sampleLcd;
        StarButton star;
        ArrowIconButton back { false }, next { true };
        juce::TextButton browse { "Browse" }, useMain { "Use Main" };
        VirusKnob attack { VirusKnob::Size::Large, "Attack" };
        VirusKnob length { VirusKnob::Size::Large, "Length" };
        VirusKnob decay  { VirusKnob::Size::Large, "Decay" };
        VirusKnob curve  { VirusKnob::Size::Large, "Curve" };
        VirusKnob fade   { VirusKnob::Size::Large, "Fade" };
        VirusKnob level  { VirusKnob::Size::Large, "Level" };
        std::array<LcdDisplay, 6> valueLcds;     // the knobs' values

        std::array<VirusKnob*, 6> knobs()        { return { &attack, &length, &decay, &curve, &fade, &level }; }
        std::vector<juce::Component*> components();
        SegmentedButtons attackModeButtons, modeButtons;
        juce::ComboBox attackSyncBox, syncBox;
        juce::TextButton mute { "M" }, solo { "S" }, phase { "Phase" };
    };

    class Content : public juce::Component,
                    private juce::ChangeListener
    {
    public:
        explicit Content (KickDrumAudioProcessor&);
        ~Content() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void updateDisplays();

    private:
        void addKnob (VirusKnob&, const juce::String& paramID, std::function<juce::String (float)> describe = nullptr,
                      kd::Unit unit = kd::Unit::Plain);
        void addToggle (juce::TextButton&, const juce::String& paramID, const juce::String& tooltip);
        void addCombo (juce::ComboBox&, const juce::String& paramID, const juce::StringArray& items, const juce::String& tooltip);
        void setupBand (BandControls&);
        void browseForFile();
        void showBrowser (int index);       // -1 = none, 0..3 = a band, kGlobalBrowser = main
        void showPage (int band);           // the band shown in the big window
        void matchTailKeyToKick();
        static void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
        static void drawBackground (juce::Graphics&, juce::Rectangle<float> area);

        KickDrumAudioProcessor& processor;
        kd::KickBank& bank;

        // Top bar
        juce::ComboBox presetBox;
        StarButton favouriteButton;
        ArrowIconButton backButton { false }, nextButton { true };
        juce::TextButton browserButton { "Browser" }, loadButton { "Load Kick" }, playButton { "Play" };

        // Kick + crossover
        kd::ui::MainWaveform mainWave;
        kd::ui::CrossoverDisplay crossover;
        SegmentedButtons slopeButtons;
        std::array<std::unique_ptr<kd::ui::FrequencyField>, 3> xoverFields;

        // Output (one hit through the whole instrument, rendered offline)
        kd::ui::OutputView outputView;
        LcdDisplay outLengthLcd, outPeakLcd;
        kd::OutputRenderer renderer;
        std::shared_ptr<const kd::RenderedHit> lastHit;

        // Band-level references (named, stored per computer)
        juce::SharedResourcePointer<LevelReferences> references;
        juce::ComboBox refBox;
        juce::TextButton saveRefButton { "Save" }, deleteRefButton { "Del" }, matchLevelsButton { "Match" };
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refreshReferenceList();
        void updateLevelStrips();
        void saveReference();
        void matchLevels();
        juce::int64 lastRenderKey = 0;
        void requestRenderIfNeeded (const kd::KickBank::DisplayInfo&);
        kd::OutputRenderer::Request makeRenderRequest (const kd::KickBank::DisplayInfo&) const;
        juce::File exportAudio();      // renders the current kick to a WAV for Drag Audio Out
        DragAudioHandle dragAudio;

        // Bands: four pages in the big window
        kd::ui::BandTabs tabs;
        std::array<std::unique_ptr<BandControls>, kd::kNumBands> bands;

        // Tail
        juce::TextButton tailOnButton { "Tail On" }, tuneBodyButton { "Tune Body" }, keyLockButton { "Key Lock" },
                         matchButton { "Match" };
        VirusKnob keyKnob       { VirusKnob::Size::Large, "Key" };
        VirusKnob tailStartKnob { VirusKnob::Size::Small, "Start" };
        SegmentedButtons tailStartModeButtons;
        juce::ComboBox tailStartSyncBox;
        VirusKnob glideKnob     { VirusKnob::Size::Small, "Glide" };
        VirusKnob tailLevelKnob { VirusKnob::Size::Small, "Level" };
        juce::ComboBox rootBox, scaleBox;
        LcdDisplay keyLcd, detectLcd;

        // Saturation
        juce::TextButton satOnButton { "On" }, satOversampleButton { "4x OS" };
        juce::ComboBox satTypeBox;
        VirusKnob driveKnob    { VirusKnob::Size::Large, "Drive" };
        VirusKnob toneKnob     { VirusKnob::Size::Small, "Tone" };
        VirusKnob satMixKnob   { VirusKnob::Size::Small, "Mix" };
        VirusKnob satLevelKnob { VirusKnob::Size::Small, "Level" };
        LcdDisplay driveLcd;
        kd::ui::SaturationCurve satCurve;

        // Compressor
        juce::TextButton compOnButton { "On" };
        kd::ui::GainReductionMeter grMeter;
        VirusKnob threshKnob      { VirusKnob::Size::Large, "Threshold" };
        VirusKnob ratioKnob       { VirusKnob::Size::Small, "Ratio" };
        VirusKnob compAttackKnob  { VirusKnob::Size::Small, "Attack" };
        VirusKnob compReleaseKnob { VirusKnob::Size::Small, "Release" };
        VirusKnob makeupKnob      { VirusKnob::Size::Small, "Makeup" };
        VirusKnob compMixKnob     { VirusKnob::Size::Small, "Mix" };
        LcdDisplay threshLcd;

        // Master
        VirusKnob volumeKnob { VirusKnob::Size::Large, "Volume" };
        VirusKnob velKnob    { VirusKnob::Size::Small, "Velocity" };
        LcdDisplay volumeLcd;
        TempoField tempoField;

        // Browsers (overlays, one visible at a time)
        BrowserPanel globalBrowser;
        std::array<std::unique_ptr<BrowserPanel>, kd::kNumBands> bandBrowsers;

        std::unique_ptr<juce::FileChooser> fileChooser;

        // Attachments last, so they are destroyed before the controls
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
        std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

        juce::Rectangle<float> kickPanel, xoverPanel, tailPanel, outputPanel, masterPanel, satPanel, compPanel,
                               bigWindow, bandPanel;
    };

    //==============================================================================
    void timerCallback() override;

    KickDrumAudioProcessor& processor;
    CustomLookAndFeel lnf;
    Content content;
    juce::TooltipWindow tooltipWindow { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickDrumAudioProcessorEditor)
};
