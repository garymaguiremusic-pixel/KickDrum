#include "FxChain.h"
#include <cmath>

using namespace juce;

namespace kd
{
    float saturate (int type, float x) noexcept
    {
        switch (type)
        {
            case 0:   // Soft: symmetric tanh, odd harmonics
                return std::tanh (x);

            case 1:   // Tape: gentle arctan knee plus a touch of even harmonics
            {
                const float s = (2.0f / MathConstants<float>::pi) * std::atan (MathConstants<float>::halfPi * x);
                return s + 0.08f * s * s;
            }

            case 2:   // Tube: asymmetric, the negative side compresses later and softer
                return x >= 0.0f ? std::tanh (x) : std::tanh (0.6f * x) / 0.6f;

            case 3:   // Hard clip
                return jlimit (-1.0f, 1.0f, x);

            case 4:   // Foldback: a sine fold, the louder the more it folds
                return std::sin (MathConstants<float>::halfPi * x);

            case 5:   // Fuzz: hard asymmetric exponential saturation
            {
                const float y = x + 0.25f;
                const float f = y >= 0.0f ? 1.0f - std::exp (-2.5f * y) : -(1.0f - std::exp (1.6f * y)) * 0.8f;
                return f - (1.0f - std::exp (-2.5f * 0.25f));
            }

            default:
                return x;
        }
    }

    //==============================================================================
    void FxChain::prepare (double sr, int maxBlock, int numChannels)
    {
        sampleRate = sr;
        channels = jlimit (1, 2, numChannels);

        oversampling = std::make_unique<dsp::Oversampling<float>> ((size_t) channels, 2,
                                                                     dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                     true, true);
        oversampling->initProcessing ((size_t) maxBlock);
        latency = roundToInt (oversampling->getLatencyInSamples());

        dryDelay.setSize (channels, jmax (1, latency + 1));
        dryCopy.setSize (channels, maxBlock);

        driveSmooth.reset (sr * 4.0, 0.02);
        mixSmooth.reset (sr, 0.02);
        levelSmooth.reset (sr, 0.02);
        makeupSmooth.reset (sr, 0.02);
        compMixSmooth.reset (sr, 0.02);
        reset();
    }

    void FxChain::reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        dryDelay.clear();
        delayWrite = 0;
        for (int ch = 0; ch < 2; ++ch)
            toneState[ch] = dcX[ch] = dcY[ch] = 0.0f;
        compEnvDb = 0.0f;
        gainReduction = 0.0f;
    }

    void FxChain::process (AudioBuffer<float>& buffer, const SaturationSettings& sat, const CompressorSettings& comp) noexcept
    {
        const int num = buffer.getNumSamples();
        const int chans = jmin (channels, buffer.getNumChannels());
        if (num == 0 || oversampling == nullptr || num > dryCopy.getNumSamples())
            return;

        // Delay line keeps the latency constant: the dry path always runs 'latency' samples late.
        for (int ch = 0; ch < chans; ++ch)
            dryCopy.copyFrom (ch, 0, buffer, ch, 0, num);

        if (latency > 0)
        {
            const int size = dryDelay.getNumSamples();
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* d = dryDelay.getWritePointer (ch);
                auto* io = dryCopy.getWritePointer (ch);
                int w = delayWrite;
                for (int i = 0; i < num; ++i)
                {
                    const float in = io[i];
                    io[i] = d[(w + size - latency) % size];
                    d[w] = in;
                    w = (w + 1) % size;
                }
            }
            delayWrite = (delayWrite + num) % size;
        }

        if (sat.on)
            processSaturation (buffer, sat);      // the oversampler's latency matches the delay
        else
            for (int ch = 0; ch < chans; ++ch)
                buffer.copyFrom (ch, 0, dryCopy, ch, 0, num);

        processCompressor (buffer, comp);
    }

    void FxChain::processSaturation (AudioBuffer<float>& buffer, const SaturationSettings& s) noexcept
    {
        const int num = buffer.getNumSamples();
        const int chans = jmin (channels, buffer.getNumChannels());

        dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) num);
        auto up = oversampling->processSamplesUp (block);

        const double osRate = sampleRate * 4.0;
        const float toneHz = 1000.0f * std::pow (20.0f, s.tone);
        const bool toneOn = s.tone < 0.995f;
        const float toneCoef = 1.0f - std::exp (-MathConstants<float>::twoPi * toneHz / (float) osRate);
        const bool asymmetric = s.type == 1 || s.type == 2 || s.type == 5;
        const float dcCoef = 1.0f - (float) (MathConstants<double>::twoPi * 5.0 / osRate);

        driveSmooth.setTargetValue (Decibels::decibelsToGain (s.driveDb));
        const int upNum = (int) up.getNumSamples();

        for (int i = 0; i < upNum; ++i)
        {
            const float drive = driveSmooth.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                float* d = up.getChannelPointer ((size_t) ch);
                float y = saturate (s.type, d[i] * drive);

                if (asymmetric)
                {
                    // 5 Hz DC blocker for the asymmetric curves
                    const float out = y - dcX[ch] + dcCoef * dcY[ch];
                    dcX[ch] = y;
                    dcY[ch] = out;
                    y = out;
                }
                if (toneOn)
                {
                    toneState[ch] += toneCoef * (y - toneState[ch]);
                    y = toneState[ch];
                }
                d[i] = y;
            }
        }

        oversampling->processSamplesDown (block);

        mixSmooth.setTargetValue (s.mix);
        levelSmooth.setTargetValue (Decibels::decibelsToGain (s.levelDb));
        for (int i = 0; i < num; ++i)
        {
            const float mix = mixSmooth.getNextValue();
            const float level = levelSmooth.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                const float wet = buffer.getSample (ch, i) * level;
                buffer.setSample (ch, i, dryCopy.getSample (ch, i) * (1.0f - mix) + wet * mix);
            }
        }
    }

    void FxChain::processCompressor (AudioBuffer<float>& buffer, const CompressorSettings& c) noexcept
    {
        const int num = buffer.getNumSamples();
        const int chans = jmin (channels, buffer.getNumChannels());

        if (! c.on)
        {
            compEnvDb = 0.0f;
            gainReduction.store (0.0f, std::memory_order_relaxed);
            return;
        }

        const float attackCoef = std::exp (-1.0f / (float) (sampleRate * jmax (0.05f, c.attackMs) * 0.001));
        const float releaseCoef = std::exp (-1.0f / (float) (sampleRate * jmax (1.0f, c.releaseMs) * 0.001));
        const float slope = 1.0f / jmax (1.0f, c.ratio) - 1.0f;
        constexpr float knee = 6.0f;

        makeupSmooth.setTargetValue (c.makeupDb);
        compMixSmooth.setTargetValue (c.mix);
        float maxGr = 0.0f;

        for (int i = 0; i < num; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                peak = jmax (peak, std::abs (buffer.getSample (ch, i)));

            const float levelDb = Decibels::gainToDecibels (peak, -120.0f);
            const float over = levelDb - c.thresholdDb;
            float target = 0.0f;   // gain change in dB (<= 0)
            if (2.0f * over <= -knee)
                target = 0.0f;
            else if (2.0f * std::abs (over) < knee)
                target = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else
                target = slope * over;

            const float coef = target < compEnvDb ? attackCoef : releaseCoef;
            compEnvDb = target + coef * (compEnvDb - target);
            maxGr = jmax (maxGr, -compEnvDb);

            const float gain = Decibels::decibelsToGain (compEnvDb + makeupSmooth.getNextValue());
            const float mix = compMixSmooth.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                const float in = buffer.getSample (ch, i);
                buffer.setSample (ch, i, in * (1.0f - mix + mix * gain));
            }
        }

        // the meter falls back slowly
        const float shown = gainReduction.load (std::memory_order_relaxed);
        gainReduction.store (jmax (maxGr, shown * 0.85f), std::memory_order_relaxed);
    }
}
