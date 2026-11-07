#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/KickBank.h"
#include "DSP/KickEngine.h"
#include "DSP/FxChain.h"
#include "Browser/SampleLibrary.h"
#include "UI/BrowserPanel.h"

//==============================================================================
class KickDrumAudioProcessor : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    static constexpr int kGlobalBrowser = kd::kNumBands;    // browser state index of the main browser

    KickDrumAudioProcessor();
    ~KickDrumAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "KickDrum"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override                    { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    kd::KickBank& getBank() noexcept                    { return bank; }
    SampleLibrary& getSampleLibrary() noexcept          { return *sampleLibrary; }
    /** 0..3 = the band browsers, kGlobalBrowser = the main one. */
    BrowserPanel::State& getBrowserState (int index) noexcept { return browserStates[(size_t) juce::jlimit (0, kd::kNumBands, index)]; }

    /** Loads into every band (Load button / drag and drop). */
    void loadGlobalFromUser (const juce::File&);
    /** Loads from a browser: index kGlobalBrowser = every band, else that band (Preview plays it). */
    void loadFromBrowser (int browserIndex, const juce::File&);

    /** Plays the kick once from the editor. bandMask bit b = band b. */
    void audition (juce::uint8 bandMask = 0x0f)         { auditionRequest.store (bandMask); }

    /** The engine settings as they are now (the editor draws the envelopes from them). */
    kd::EngineSettings makeEngineSettings() const;
    int getTailNote() const;     // the scale-locked tail key

    double getPlayheadSeconds() const noexcept          { return playhead.load(); }
    float getGainReductionDb() const noexcept           { return fx.getGainReductionDb(); }
    float getOutputPeak() noexcept                      { return outputPeak.exchange (0.0f); }
    float getTempo() const noexcept                     { return tempo.load(); }
    bool isTempoFromHost() const noexcept               { return tempoFromHost.load(); }

    juce::Point<int> getEditorSize() const noexcept     { return editorSize; }
    void setEditorSize (juce::Point<int> s) noexcept    { editorSize = s; }
    int getOpenBrowser() const noexcept                 { return openBrowser; }
    void setOpenBrowser (int index) noexcept            { openBrowser = index; }

private:
    void timerCallback() override;      // follows the crossover parameters
    kd::SaturationSettings makeSaturationSettings() const;
    kd::CompressorSettings makeCompressorSettings() const;

    kd::KickBank bank;
    kd::KickEngine engine;
    kd::FxChain fx;

    struct ParamPointers;
    std::unique_ptr<ParamPointers> params;    // cached raw parameter values (no lookups on the audio thread)

    std::atomic<int> auditionRequest { -1 };
    juce::uint32 lastSetId = 0;
    std::atomic<double> playhead { -1.0 };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> tempo { 120.0f };
    std::atomic<bool> tempoFromHost { false };
    juce::SmoothedValue<float> volumeSmooth;

    int currentProgram = 0;
    juce::Point<int> editorSize;
    int openBrowser = -1;
    juce::SharedResourcePointer<SampleLibrary> sampleLibrary;
    std::array<BrowserPanel::State, kd::kNumBands + 1> browserStates;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickDrumAudioProcessor)
};
