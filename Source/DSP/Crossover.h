#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace kd::dsp
{
    /**
        Linear-phase (zero-phase) 4-band crossover for a sample that is already in memory.

        Each band is the sample's spectrum multiplied by a real, non-negative gain
        curve, done with one large FFT over the whole sample, so:
          - there is no phase shift at all (no group delay, no smearing of the
            transient between bands);
          - the four gain curves add up to exactly 1 at every frequency, so the four
            bands add back to the original sample (to float precision).

        Slope: a crossover of order k uses   low(f) = 1 / (1 + (f/fc)^k),  high = 1 - low.
        That falls at 6*k dB/oct and is -6 dB at fc (like a Linkwitz-Riley crossover).
        Gentler slopes ring for less time, steeper ones separate the bands more.
    */
    struct CrossoverSettings
    {
        std::array<float, 3> freqs { 120.0f, 1000.0f, 10000.0f };
        int slopeIndex = 3;     // 6, 12, 18, 24, 36 dB/oct

        bool operator== (const CrossoverSettings& o) const noexcept  { return freqs == o.freqs && slopeIndex == o.slopeIndex; }
        bool operator!= (const CrossoverSettings& o) const noexcept  { return ! operator== (o); }

        /** Keeps the frequencies in order, at least a third of an octave apart and below 'nyquist'. */
        CrossoverSettings sanitised (double sampleRate) const noexcept;
    };

    int slopeOrder (int slopeIndex) noexcept;

    /** The zero-phase gain of 'band' (0..3) at 'freqHz'. The four bands sum to 1. */
    float bandGain (int band, double freqHz, const CrossoverSettings&) noexcept;

    /**
        Splits 'source' into the requested bands (bandsWanted[b] = true), writing each
        into outputs[b] with the same channel count and length as the source.
        Message/background thread only (allocates).
    */
    void splitIntoBands (const juce::AudioBuffer<float>& source, double sampleRate, const CrossoverSettings&,
                         const std::array<bool, 4>& bandsWanted, std::array<juce::AudioBuffer<float>, 4>& outputs);
}
