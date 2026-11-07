#include "Crossover.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace kd::dsp
{
    int slopeOrder (int slopeIndex) noexcept
    {
        static constexpr int orders[] { 1, 2, 3, 4, 6 };
        return orders[juce::jlimit (0, 4, slopeIndex)];
    }

    CrossoverSettings CrossoverSettings::sanitised (double sampleRate) const noexcept
    {
        CrossoverSettings s = *this;
        const float minRatio = std::pow (2.0f, 1.0f / 3.0f);
        const float top = (float) (sampleRate * 0.45);

        s.freqs[0] = juce::jlimit (20.0f, top / (minRatio * minRatio), s.freqs[0]);
        s.freqs[1] = juce::jlimit (s.freqs[0] * minRatio, top / minRatio, s.freqs[1]);
        s.freqs[2] = juce::jlimit (s.freqs[1] * minRatio, top, s.freqs[2]);
        s.slopeIndex = juce::jlimit (0, 4, s.slopeIndex);
        return s;
    }

    namespace
    {
        /** Low-pass part of one crossover point. */
        inline double lowPart (double f, double fc, int order) noexcept
        {
            if (f <= 0.0)
                return 1.0;
            return 1.0 / (1.0 + std::pow (f / fc, (double) order));
        }
    }

    float bandGain (int band, double f, const CrossoverSettings& s) noexcept
    {
        const int k = slopeOrder (s.slopeIndex);
        const double l1 = lowPart (f, s.freqs[0], k);
        const double l2 = lowPart (f, s.freqs[1], k);
        const double l3 = lowPart (f, s.freqs[2], k);

        // Telescoping split: each band takes the low part of what the bands below left over.
        switch (band)
        {
            case 0:  return (float) l1;
            case 1:  return (float) ((1.0 - l1) * l2);
            case 2:  return (float) ((1.0 - l1) * (1.0 - l2) * l3);
            default: return (float) ((1.0 - l1) * (1.0 - l2) * (1.0 - l3));
        }
    }

    void splitIntoBands (const juce::AudioBuffer<float>& source, double sampleRate, const CrossoverSettings& settings,
                         const std::array<bool, 4>& wanted, std::array<juce::AudioBuffer<float>, 4>& outputs)
    {
        const int numCh = source.getNumChannels();
        const int len = source.getNumSamples();

        for (int b = 0; b < 4; ++b)
            if (wanted[b])
            {
                outputs[b].setSize (numCh, len, false, false, false);
                outputs[b].clear();
            }

        if (len == 0 || numCh == 0)
            return;

        // Room for the filters' ringing on both sides so the circular FFT doesn't wrap
        // the end of the sample onto its start: 0.5 s, or more for long samples.
        const int margin = juce::jmax (4096, (int) (sampleRate * 0.5));
        int order = 1;
        while ((1 << order) < len + 2 * margin)
            ++order;
        const int n = 1 << order;

        juce::dsp::FFT fft (order);
        std::vector<float> spectrum ((size_t) n * 2), work ((size_t) n * 2);

        // gain curve per bin (bins 0 .. n/2)
        const auto s = settings.sanitised (sampleRate);
        std::array<std::vector<float>, 4> gains;
        for (int b = 0; b < 4; ++b)
        {
            if (! wanted[b])
                continue;
            gains[b].resize ((size_t) n / 2 + 1);
            for (int i = 0; i <= n / 2; ++i)
                gains[b][(size_t) i] = bandGain (b, (double) i * sampleRate / (double) n, s);
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            std::copy (source.getReadPointer (ch), source.getReadPointer (ch) + len, spectrum.begin());
            fft.performRealOnlyForwardTransform (spectrum.data(), true);

            for (int b = 0; b < 4; ++b)
            {
                if (! wanted[b])
                    continue;

                std::fill (work.begin(), work.end(), 0.0f);
                for (int i = 0; i <= n / 2; ++i)
                {
                    const float g = gains[b][(size_t) i];
                    work[(size_t) i * 2]     = spectrum[(size_t) i * 2] * g;
                    work[(size_t) i * 2 + 1] = spectrum[(size_t) i * 2 + 1] * g;
                }
                // mirror the negative frequencies (conjugates) for the inverse transform
                for (int i = n / 2 + 1; i < n; ++i)
                {
                    work[(size_t) i * 2]     =  work[(size_t) (n - i) * 2];
                    work[(size_t) i * 2 + 1] = -work[(size_t) (n - i) * 2 + 1];
                }

                fft.performRealOnlyInverseTransform (work.data());

                // Zero-phase output: time 0 stays at index 0. The pre-ringing before the
                // sample (which lands at the end of the FFT buffer) and the ringing after it
                // are dropped; across the four bands those parts add up to silence anyway.
                std::copy (work.begin(), work.begin() + len, outputs[b].getWritePointer (ch));
            }
        }
    }
}
