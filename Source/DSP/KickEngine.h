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
        float curve = 0.4f;                              // -1 = slow start, 0 = linear, +1 = steep exponential
        float gain = 1.0f;                               // linear
        double fade = 0.02;                              // seconds: fade-out before the end of the sample (End point)
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
        double startFrac = 0.0, endFrac = 1.0;   // the Start / End points, as fractions of each band's sample
    };

    /** The decay shape: x goes 0 -> 1 over the decay, the result 1 -> 0. Always lands softly on 0.
        curve > 0: exponential (fast drop, long gentle tail), curve < 0: slow start, 0: linear. */
    float decayShape (double x, float curve) noexcept;

    /** The fades at the edges of the played part of a sample: a short fade-in when playback starts
        after the beginning (Start > 0), and the band's Fade (raised cosine) that reaches silence
        exactly at the End point. */
    float regionFade (double secondsFromStart, double secondsToEnd, double fadeOut, bool fadeIn) noexcept;
    static constexpr double kStartFadeIn = 0.002;

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
        static constexpr double kChokeSeconds = 0.006;       // a new hit fades the old one out over this
        static constexpr double kTailCrossfade = 0.03;
        static constexpr double kEnvelopeGlide = 0.015;      // envelope edits glide (time constant), never jump
        static constexpr double kGainGlide = 0.01;           // level / mute / solo changes fade over ~10 ms
        static constexpr double kSetCrossfade = 0.01;        // re-split bands (crossover moved) crossfade in

        void prepare (double sampleRate);
        void reset();

        /** 'bandMask': bit b set = band b plays (the editor's per-band audition uses a single band). */
        void trigger (const KickSet*, const EngineSettings&, float velocity, juce::uint8 bandMask = 0x0f);

        /** Adds the hits into 'out' (stereo or mono). When 'set' changes while hits are playing
            (the crossover moved), they crossfade from 'previous' (which must stay valid until
            isCrossfadingSets() is false). */
        void render (const KickSet*, const EngineSettings&, juce::AudioBuffer<float>& out, int start, int num,
                     const KickSet* previous = nullptr) noexcept;
        bool isCrossfadingSets() const noexcept     { return setFade < 1.0f; }

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

            // the envelope this hit actually plays: glides towards the knob settings
            std::array<BandSettings, kNumBands> env;
            double startFrac = 0.0;          // fixed for the hit (moving it mid-note would jump)
            double endFrac = 1.0;            // glides like the envelope

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
        std::array<float, kNumBands> bandGain { 1.0f, 1.0f, 1.0f, 1.0f };   // smoothed level (0 = muted)
        bool gainsInitialised = false;
        float envCoef = 0.0f, gainCoef = 0.0f;

        const KickSet* lastSet = nullptr;
        float setFade = 1.0f, setFadeStep = 0.0f;
    };
}
