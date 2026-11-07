#pragma once

#include <juce_core/juce_core.h>

namespace kd::dsp
{
    struct PitchResult
    {
        double hz = 0.0;           // 0 = nothing found
        double confidence = 0.0;   // 0..1 (peak of the normalised autocorrelation)

        bool isValid() const noexcept   { return hz > 0.0; }
    };

    /**
        Finds the fundamental of a kick's body/tail (25..250 Hz) with a normalised
        autocorrelation (McLeod's NSDF) over the part after the attack.
        'data' should be the low band (or a mono kick). Any thread; allocates.
    */
    PitchResult detectKickPitch (const float* data, int numSamples, double sampleRate);

    /**
        Amplitude and phase of a sinusoid of 'hz' around 'centre' (in samples), measured
        over two periods with a Hann window:  x[centre + n] ~= amp * cos (w n + phase).
        Real-time safe (no allocation); at most 'maxWindow' samples are read.
    */
    void measureSinusoid (const float* data, int numSamples, double centre, double hz, double sampleRate,
                          float& amp, double& phase, int maxWindow = 8192) noexcept;
}
