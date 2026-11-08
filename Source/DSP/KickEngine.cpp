#include "KickEngine.h"
#include <cmath>

using namespace juce;

namespace kd
{
    float envelopeAt (const BandSettings& s, double t) noexcept
    {
        if (t < 0.0)
            return 0.0f;
        if (t < s.attack)
            return (float) (t / s.attack);
        t -= s.attack;
        if (t < s.hold)
            return 1.0f;
        t -= s.hold;
        if (t >= s.decay || s.decay <= 0.0)
            return 0.0f;

        return decayShape (t / s.decay, s.curve);
    }

    float decayShape (double x, float curve) noexcept
    {
        x = jlimit (0.0, 1.0, x);
        const double k = 9.0 * std::abs ((double) jlimit (-1.0f, 1.0f, curve));
        if (k < 1.0e-3)
            return (float) (1.0 - x);

        // a true exponential, rescaled so it starts at 1 and ends exactly at 0
        const double floor = std::exp (-k);
        auto expo = [k, floor] (double u) { return (std::exp (-k * u) - floor) / (1.0 - floor); };
        return (float) (curve > 0.0f ? expo (x) : 1.0 - expo (1.0 - x));
    }

    double tuneRatio (const TailSettings& tail, const dsp::PitchResult& pitch) noexcept
    {
        if (! tail.tuneBody || ! pitch.isValid())
            return 1.0;

        // nearest octave of the key to the kick's own pitch, so it never shifts more than 6 semitones
        double target = tail.keyHz;
        while (target / pitch.hz > std::sqrt (2.0)) target *= 0.5;
        while (pitch.hz / target > std::sqrt (2.0)) target *= 2.0;
        return target / pitch.hz;
    }

    //==============================================================================
    void KickEngine::prepare (double sr)
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        reset();
    }

    void KickEngine::reset()
    {
        for (auto& v : voices)
            v = Voice();
    }

    void KickEngine::trigger (const KickSet* set, const EngineSettings& settings, float velocity, uint8 bandMask)
    {
        // fade out whatever is playing
        for (auto& v : voices)
            if (v.active)
                v.choking = true;

        // a free voice, else the oldest one
        Voice* voice = nullptr;
        for (auto& v : voices)
            if (! v.active)
            {
                voice = &v;
                break;
            }
        if (voice == nullptr)
        {
            voice = &voices[0];
            for (auto& v : voices)
                if (v.order < voice->order)
                    voice = &v;
        }

        Voice v;
        v.active = true;
        v.order = nextOrder++;
        v.mask = bandMask;
        const float sens = jlimit (0.0f, 1.0f, settings.velocitySensitivity);
        v.velocityGain = 1.0f - sens * (1.0f - jlimit (0.0f, 1.0f, velocity));

        const dsp::PitchResult pitch = set != nullptr ? set->pitch : dsp::PitchResult();
        v.ratio = tuneRatio (settings.tail, pitch);

        // Tail: measure the Sub band's own sinusoid where the hand-over happens, so the tail
        // oscillator continues it at the same phase and level (no dip, no click).
        if (set != nullptr && settings.tail.on && pitch.isValid())
        {
            const auto& sub = set->bands[0];
            v.tail = true;
            v.tailStart = jmax (kTailCrossfade, settings.tail.start);
            v.tailGlide = settings.tail.glide;
            v.tailFrom = pitch.hz * v.ratio;              // the Sub's pitch as heard
            v.tailTo = settings.tail.keyHz;

            // with Tune Body on, the body is already in the key's nearest octave: stay there
            if (settings.tail.tuneBody)
                v.tailTo = v.tailFrom;

            const double srcPos = v.tailStart * v.ratio * sub.sampleRate;
            const int chans = jmin (2, sub.audio.getNumChannels());
            for (int ch = 0; ch < chans; ++ch)
            {
                double phaseAtStart = 0.0;
                dsp::measureSinusoid (sub.audio.getReadPointer (ch), sub.length(), srcPos, pitch.hz, sub.sampleRate,
                                      v.tailAmp[(size_t) ch], phaseAtStart);
                // the oscillator starts half a crossfade earlier
                v.tailPhase[(size_t) ch] = phaseAtStart - MathConstants<double>::twoPi * v.tailFrom * (kTailCrossfade * 0.5);
            }
            if (chans == 1)
            {
                v.tailAmp[1] = v.tailAmp[0];
                v.tailPhase[1] = v.tailPhase[0];
            }
        }

        *voice = v;
    }

    float KickEngine::readSample (const BandAudio& band, int channel, double pos) noexcept
    {
        const int len = band.length();
        const int i = (int) pos;
        if (pos < 0.0 || i >= len)
            return 0.0f;

        const float* d = band.audio.getReadPointer (jmin (channel, band.audio.getNumChannels() - 1));
        const float frac = (float) (pos - i);
        auto at = [d, len] (int k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };

        // 4-point, 3rd-order Hermite
        const float xm1 = at (i - 1), x0 = d[i], x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }

    void KickEngine::render (const KickSet* set, const EngineSettings& settings, AudioBuffer<float>& out, int start, int num) noexcept
    {
        if (num <= 0)
            return;

        const int outChans = jmin (2, out.getNumChannels());
        const double dt = 1.0 / sampleRate;
        const float chokeStep = (float) (dt / kChokeSeconds);

        // per-block linear ramps of the band levels (no zipper noise when automating)
        std::array<float, kNumBands> gainFrom = lastGain, gainStep {};
        for (int b = 0; b < kNumBands; ++b)
        {
            const float target = settings.bands[(size_t) b].enabled ? settings.bands[(size_t) b].gain : 0.0f;
            gainStep[(size_t) b] = (target - gainFrom[(size_t) b]) / (float) num;
            lastGain[(size_t) b] = target;
        }

        double longest = 0.0;
        for (auto& b : settings.bands)
            longest = jmax (longest, b.length());

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            if (set == nullptr)
            {
                v.active = false;
                continue;
            }

            for (int i = 0; i < num; ++i)
            {
                if (v.choking)
                {
                    v.choke -= chokeStep;
                    if (v.choke <= 0.0f)
                    {
                        v.active = false;
                        break;
                    }
                }

                if (v.t > longest)
                {
                    v.active = false;
                    break;
                }

                float left = 0.0f, right = 0.0f;

                for (int b = 0; b < kNumBands; ++b)
                {
                    if ((v.mask & (1 << b)) == 0)
                        continue;

                    const auto& bs = settings.bands[(size_t) b];
                    const float g = (gainFrom[(size_t) b] + gainStep[(size_t) b] * (float) i);
                    if (g <= 0.0f)
                        continue;

                    const float env = envelopeAt (bs, v.t);
                    if (env <= 0.0f)
                        continue;

                    const auto& band = set->bands[(size_t) b];
                    const double pos = v.t * v.ratio * band.sampleRate;

                    float l = readSample (band, 0, pos);
                    float r = band.audio.getNumChannels() > 1 ? readSample (band, 1, pos) : l;

                    if (b == 0 && v.tail)
                    {
                        // hand over from the sample to the tail oscillator
                        const double xfStart = v.tailStart - kTailCrossfade * 0.5;
                        const float w = (float) jlimit (0.0, 1.0, (v.t - xfStart) / kTailCrossfade);
                        if (w > 0.0f)
                        {
                            const float tl = settings.tail.level;
                            const float oscL = v.tailAmp[0] * (float) std::cos (v.tailPhase[0]);
                            const float oscR = v.tailAmp[1] * (float) std::cos (v.tailPhase[1]);
                            // the sample fades out; the tail fades in, then follows Tail Level
                            const float tailGain = w * (1.0f + (tl - 1.0f) * w);
                            l = l * (1.0f - w) + oscL * tailGain;
                            r = r * (1.0f - w) + oscR * tailGain;
                        }
                    }

                    const float gain = env * g;
                    left += l * gain;
                    right += r * gain;
                }

                // advance the tail oscillator: the kick's own pitch, then a glide to the key
                if (v.tail && v.t >= v.tailStart - kTailCrossfade * 0.5)
                {
                    double f = v.tailFrom;
                    if (v.t > v.tailStart)
                    {
                        const double x = v.tailGlide > 0.0 ? jmin (1.0, (v.t - v.tailStart) / v.tailGlide) : 1.0;
                        f = v.tailFrom * std::pow (v.tailTo / v.tailFrom, x);
                    }
                    const double inc = MathConstants<double>::twoPi * f * dt;
                    for (auto& p : v.tailPhase)
                    {
                        p += inc;
                        if (p > MathConstants<double>::twoPi)
                            p -= MathConstants<double>::twoPi;
                    }
                }

                const float g = v.velocityGain * v.choke;
                if (outChans == 1)
                {
                    out.addSample (0, start + i, 0.5f * (left + right) * g);
                }
                else
                {
                    out.addSample (0, start + i, left * g);
                    out.addSample (1, start + i, right * g);
                }

                v.t += dt;
            }
        }
    }

    double KickEngine::getPlayheadSeconds() const noexcept
    {
        const Voice* newest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.choking && (newest == nullptr || v.order > newest->order))
                newest = &v;
        return newest != nullptr ? newest->t : -1.0;
    }

    bool KickEngine::isPlaying() const noexcept
    {
        for (auto& v : voices)
            if (v.active)
                return true;
        return false;
    }
}
