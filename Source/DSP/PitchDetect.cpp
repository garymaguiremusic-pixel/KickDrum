#include "PitchDetect.h"
#include <cmath>
#include <vector>

namespace kd::dsp
{
    PitchResult detectKickPitch (const float* data, int numSamples, double sampleRate)
    {
        PitchResult result;
        if (data == nullptr || numSamples <= 0 || sampleRate <= 0.0)
            return result;

        // Decimate to ~2.4 kHz (the input is the low band, so there's little to alias).
        const int decim = std::max (1, (int) (sampleRate / 2400.0));
        const double rate = sampleRate / decim;

        // Skip the attack (the click and the fast pitch drop, ~80 ms), then take up to 300 ms.
        const int start = std::min ((int) (sampleRate * 0.08), numSamples / 4);
        const int available = (numSamples - start) / decim;
        const int window = std::min (available, (int) (rate * 0.3));

        const int minLag = std::max (2, (int) (rate / 250.0));
        const int maxLag = (int) (rate / 25.0);
        if (window < minLag * 3)
            return result;

        std::vector<double> x ((size_t) window);
        double energy = 0.0;
        for (int i = 0; i < window; ++i)
        {
            x[(size_t) i] = data[start + i * decim];
            energy += x[(size_t) i] * x[(size_t) i];
        }
        if (energy < 1.0e-9)
            return result;

        const int lastLag = std::min (maxLag, window - minLag);
        std::vector<double> nsdf ((size_t) lastLag + 2, 0.0);
        for (int tau = 0; tau <= lastLag + 1 && tau < window; ++tau)
        {
            double acf = 0.0, m = 0.0;
            for (int i = 0; i + tau < window; ++i)
            {
                acf += x[(size_t) i] * x[(size_t) (i + tau)];
                m += x[(size_t) i] * x[(size_t) i] + x[(size_t) (i + tau)] * x[(size_t) (i + tau)];
            }
            nsdf[(size_t) tau] = m > 0.0 ? 2.0 * acf / m : 0.0;
        }

        // McLeod: key maxima between positive-going zero crossings, take the first one
        // that reaches 85 % of the highest.
        std::vector<std::pair<int, double>> peaks;
        int tau = 1;
        while (tau <= lastLag && nsdf[(size_t) tau] > 0.0)   // skip the zero-lag lobe
            ++tau;
        while (tau <= lastLag)
        {
            while (tau <= lastLag && nsdf[(size_t) tau] <= 0.0)
                ++tau;
            int best = -1;
            while (tau <= lastLag && nsdf[(size_t) tau] > 0.0)
            {
                if (tau >= minLag && (best < 0 || nsdf[(size_t) tau] > nsdf[(size_t) best]))
                    best = tau;
                ++tau;
            }
            if (best > 0)
                peaks.push_back ({ best, nsdf[(size_t) best] });
        }
        if (peaks.empty())
            return result;

        double highest = 0.0;
        for (auto& p : peaks)
            highest = std::max (highest, p.second);

        for (auto& [lag, value] : peaks)
        {
            if (value < highest * 0.85)
                continue;

            // parabolic interpolation around the peak
            double refined = lag;
            if (lag > 0 && lag < (int) nsdf.size() - 1)
            {
                const double a = nsdf[(size_t) lag - 1], b = nsdf[(size_t) lag], c = nsdf[(size_t) lag + 1];
                const double denom = a - 2.0 * b + c;
                if (std::abs (denom) > 1.0e-12)
                    refined = lag + 0.5 * (a - c) / denom;
            }

            if (value < 0.3)
                return result;   // too noisy to call a pitch

            result.hz = rate / refined;
            result.confidence = std::min (1.0, value);
            return result;
        }
        return result;
    }

    void measureSinusoid (const float* data, int numSamples, double centre, double hz, double sampleRate,
                          float& amp, double& phase, int maxWindow) noexcept
    {
        amp = 0.0f;
        phase = 0.0;
        if (data == nullptr || numSamples <= 0 || hz <= 0.0)
            return;

        const int half = std::min (maxWindow / 2, (int) std::ceil (sampleRate / hz));    // one period each side
        const int c = (int) std::floor (centre);
        const double frac = centre - c;
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;

        // rotate a phasor instead of calling sin/cos per sample
        double re = 0.0, im = 0.0, wsum = 0.0;
        const double startAngle = w * (-half - frac);
        double cr = std::cos (startAngle), ci = -std::sin (startAngle);   // e^{-i w (n - centre)}
        const double dr = std::cos (w), di = -std::sin (w);

        for (int n = -half; n <= half; ++n)
        {
            const int idx = c + n;
            const double hann = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (double) n / (double) (half + 1));
            if (idx >= 0 && idx < numSamples)
            {
                const double v = data[idx] * hann;
                re += v * cr;
                im += v * ci;
            }
            wsum += hann;

            const double nr = cr * dr - ci * di;
            ci = cr * di + ci * dr;
            cr = nr;
        }

        if (wsum <= 0.0)
            return;
        amp = (float) (2.0 * std::sqrt (re * re + im * im) / wsum);
        phase = std::atan2 (im, re);
    }
}
