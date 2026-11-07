#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "KickBank.h"

namespace kd
{
    //==============================================================================
    struct BandSettings
    {
        double attack = 0.0, hold = 0.3, decay = 0.6;    // seconds (decay already resolved for Sync)
        float curve = 0.5f;                              // 0 = linear decay .. 1 = steep exponential-like
        float gain = 1.0f;                               // linear
        bool enabled = true;                             // after mute / solo

        double length() const noexcept    { return attack + hold + decay; }
    };

    struct TailSettings
    {
        bool on = false;
        double keyHz = 55.0;       // the (scale-locked) tail key
        double start = 0.12;       // seconds: where the Sub band's sample hands over to the tail
        double glide = 0.06;       // seconds: from the kick's own pitch to the key
        float level = 1.0f;        // linear
        bool tuneBody = false;     // re-pitch the whole kick to the key
    };

    struct EngineSettings
    {
        std::array<BandSettings, kNumBands> bands;
        TailSettings tail;
        float velocitySensitivity = 0.5f;   // 0..1
    };

    /** Gain of a band's Attack / Hold / Decay envelope at 't' seconds after the hit. */
    float envelopeAt (const BandSettings&, double t) noexcept;

    /** Tune Body playback ratio for a detected pitch (1 when off or nothing was detected). */
    double tuneRatio (const TailSettings&, const dsp::PitchResult&) noexcept;

    //==============================================================================
    /**
        Plays the four bands of a KickSet. Every MIDI note (or preview) starts a hit;
        a new hit fades the previous one out over a few milliseconds so they never click.
        Real-time safe.
    */
    class KickEngine
    {
    public:
        static constexpr int kMaxVoices = 4;
        static constexpr double kChokeSeconds = 0.004;
        static constexpr double kTailCrossfade = 0.03;

        void prepare (double sampleRate);
        void reset();

        /** 'bandMask': bit b set = band b plays (the editor's per-band audition uses a single band). */
        void trigger (const KickSet*, const EngineSettings&, float velocity, juce::uint8 bandMask = 0x0f);

        /** Adds the hits into 'out' (stereo or mono). */
        void render (const KickSet*, const EngineSettings&, juce::AudioBuffer<float>& out, int start, int num) noexcept;

        /** Seconds since the newest hit that is still playing, or -1. */
        double getPlayheadSeconds() const noexcept;
        bool isPlaying() const noexcept;

    private:
        struct Voice
        {
            bool active = false;
            double t = 0.0;                  // seconds since the hit
            double ratio = 1.0;              // Tune Body playback speed
            float velocityGain = 1.0f;
            juce::uint8 mask = 0x0f;
            juce::uint32 order = 0;

            bool choking = false;
            float choke = 1.0f;

            // tail oscillator (Sub band)
            bool tail = false;
            double tailStart = 0.0, tailGlide = 0.0, tailFrom = 0.0, tailTo = 0.0;
            std::array<float, 2> tailAmp {};
            std::array<double, 2> tailPhase {};
        };

        static inline float readSample (const BandAudio&, int channel, double position) noexcept;

        double sampleRate = 44100.0;
        std::array<Voice, kMaxVoices> voices;
        juce::uint32 nextOrder = 1;
        std::array<float, kNumBands> lastGain { 1.0f, 1.0f, 1.0f, 1.0f };
    };
}
