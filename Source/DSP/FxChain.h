#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace kd
{
    struct SaturationSettings
    {
        bool on = false;
        int type = 0;              // Soft, Tape, Tube, Hard Clip, Foldback, Fuzz
        float driveDb = 6.0f;
        float tone = 1.0f;         // 0 = dark .. 1 = open
        float mix = 1.0f;          // 0..1
        float levelDb = 0.0f;      // wet level
    };

    struct CompressorSettings
    {
        bool on = false;
        float thresholdDb = -12.0f, ratio = 4.0f;
        float attackMs = 10.0f, releaseMs = 120.0f;
        float makeupDb = 0.0f, mix = 1.0f;
    };

    /** The saturation curve on its own (exposed for tests and the editor's curve display). */
    float saturate (int type, float x) noexcept;

    //==============================================================================
    /**
        Saturation (4x oversampled with linear-phase FIR half-band filters, so it adds
        no phase smear of its own) followed by a clean feed-forward compressor
        (stereo-linked peak detector, soft knee, no colouration).

        The latency is constant (the dry signal is delayed to match) and reported to the
        host, so the kick stays on the grid whether Saturation is on or off.
    */
    class FxChain
    {
    public:
        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset();
        int getLatencySamples() const noexcept     { return latency; }

        void process (juce::AudioBuffer<float>&, const SaturationSettings&, const CompressorSettings&) noexcept;

        float getGainReductionDb() const noexcept  { return gainReduction.load (std::memory_order_relaxed); }

    private:
        void processSaturation (juce::AudioBuffer<float>&, const SaturationSettings&) noexcept;
        void processCompressor (juce::AudioBuffer<float>&, const CompressorSettings&) noexcept;

        double sampleRate = 44100.0;
        int channels = 2, latency = 0;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

        juce::AudioBuffer<float> dryDelay;      // latency compensation for the dry path
        int delayWrite = 0;
        juce::AudioBuffer<float> dryCopy;

        float toneState[2] {}, dcX[2] {}, dcY[2] {};
        juce::SmoothedValue<float> driveSmooth, mixSmooth, levelSmooth;

        float compEnvDb = 0.0f;
        juce::SmoothedValue<float> makeupSmooth, compMixSmooth;
        std::atomic<float> gainReduction { 0.0f };
    };
}
