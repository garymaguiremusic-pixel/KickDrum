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

    float regionFade (double fromStart, double toEnd, double fadeOut, bool fadeIn) noexcept
    {
        if (toEnd <= 0.0)
            return 0.0f;
        float g = 1.0f;
        if (fadeOut > 0.0 && toEnd < fadeOut)
            g = 0.5f - 0.5f * (float) std::cos (MathConstants<double>::pi * toEnd / fadeOut);
        if (fadeIn && fromStart < kStartFadeIn)
            g *= 0.5f - 0.5f * (float) std::cos (MathConstants<double>::pi * jmax (0.0, fromStart) / kStartFadeIn);
        return g;
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
        envCoef = (float) (1.0 - std::exp (-1.0 / (sampleRate * kEnvelopeGlide)));
        gainCoef = (float) (1.0 - std::exp (-1.0 / (sampleRate * kGainGlide)));
        setFadeStep = (float) (1.0 / (sampleRate * kSetCrossfade));
        reset();
    }

    void KickEngine::reset()
    {
        for (auto& v : voices)
            v = Voice();
        gainsInitialised = false;
        setFade = 1.0f;
        lastSet = nullptr;
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
        v.env = settings.bands;     // a new hit starts exactly on the current settings
        v.startFrac = jlimit (0.0, 1.0, settings.startFrac);
        v.endFrac = jlimit (v.startFrac, 1.0, settings.endFrac);
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

            const double srcPos = v.startFrac * sub.length() + v.tailStart * v.ratio * sub.sampleRate;
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

    void KickEngine::render (const KickSet* set, const EngineSettings& settings, AudioBuffer<float>& out, int start, int num,
                             const KickSet* previous) noexcept
    {
        if (num <= 0)
            return;

        const int outChans = jmin (2, out.getNumChannels());
        const double dt = 1.0 / sampleRate;
        const float chokeStep = (float) (dt / kChokeSeconds);

        // the band split changed under playing hits: crossfade from the old buffers
        if (set != lastSet)
        {
            setFade = (lastSet != nullptr && previous != nullptr && isPlaying()) ? 0.0f : 1.0f;
            lastSet = set;
        }
        if (previous == nullptr)
            setFade = 1.0f;

        std::array<float, kNumBands> targetGain {};
        for (int b = 0; b < kNumBands; ++b)
            targetGain[(size_t) b] = settings.bands[(size_t) b].enabled ? settings.bands[(size_t) b].gain : 0.0f;
        if (! gainsInitialised)
        {
            bandGain = targetGain;
            gainsInitialised = true;
        }

        double targetLongest = 0.0;
        for (auto& b : settings.bands)
            targetLongest = jmax (targetLongest, b.length());

        // per-sample level smoothing, shared by every hit (sample-accurate, no zipper or click)
        const std::array<float, kNumBands> gainAtStart = bandGain;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            if (set == nullptr)
            {
                v.active = false;
                continue;
            }

            std::array<float, kNumBands> g = gainAtStart;
            float fade = setFade;

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

                // the hit's envelope glides towards the knobs (edits never make it jump)
                double longest = targetLongest;
                for (int b = 0; b < kNumBands; ++b)
                {
                    auto& e = v.env[(size_t) b];
                    const auto& target = settings.bands[(size_t) b];
                    e.attack += (target.attack - e.attack) * envCoef;
                    e.hold   += (target.hold - e.hold) * envCoef;
                    e.decay  += (target.decay - e.decay) * envCoef;
                    e.curve  += (target.curve - e.curve) * envCoef;
                    e.fade   += (target.fade - e.fade) * envCoef;
                    longest = jmax (longest, e.length());
                    g[(size_t) b] += (targetGain[(size_t) b] - g[(size_t) b]) * gainCoef;
                }
                if (fade < 1.0f)
                    fade = jmin (1.0f, fade + setFadeStep);
                v.endFrac += (jlimit (v.startFrac, 1.0, settings.endFrac) - v.endFrac) * envCoef;

                if (v.t > longest + 0.01)
                {
                    v.active = false;
                    break;
                }

                float left = 0.0f, right = 0.0f;

                for (int b = 0; b < kNumBands; ++b)
                {
                    if ((v.mask & (1 << b)) == 0)
                        continue;

                    const float gain = g[(size_t) b];
                    if (gain <= 1.0e-6f)
                        continue;

                    const float env = envelopeAt (v.env[(size_t) b], v.t);
                    if (env <= 0.0f)
                        continue;

                    // the played part of the sample: Start .. End, with the fades at its edges
                    auto play = [&v] (const BandAudio& band, double fadeOut, float& outL, float& outR)
                    {
                        const double len = band.length();
                        const double startPos = v.startFrac * len;
                        const double pos = startPos + v.t * v.ratio * band.sampleRate;
                        const double toEnd = (v.endFrac * len - pos) / (band.sampleRate * v.ratio);
                        const float edge = regionFade (v.t, toEnd, fadeOut, startPos > 0.5);
                        if (edge <= 0.0f)
                        {
                            outL = outR = 0.0f;
                            return;
                        }
                        outL = readSample (band, 0, pos) * edge;
                        outR = (band.audio.getNumChannels() > 1 ? readSample (band, 1, pos) : readSample (band, 0, pos)) * edge;
                    };

                    float l = 0.0f, r = 0.0f;
                    play (set->bands[(size_t) b], v.env[(size_t) b].fade, l, r);

                    if (fade < 1.0f && previous != nullptr)
                    {
                        float ol = 0.0f, orr = 0.0f;
                        play (previous->bands[(size_t) b], v.env[(size_t) b].fade, ol, orr);
                        l = l * fade + ol * (1.0f - fade);
                        r = r * fade + orr * (1.0f - fade);
                    }

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

                    left += l * env * gain;
                    right += r * env * gain;
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

                const float vg = v.velocityGain * v.choke;
                if (outChans == 1)
                {
                    out.addSample (0, start + i, 0.5f * (left + right) * vg);
                }
                else
                {
                    out.addSample (0, start + i, left * vg);
                    out.addSample (1, start + i, right * vg);
                }

                v.t += dt;
            }
        }

        // advance the shared smoothers over the block (also when nothing is playing)
        for (int i = 0; i < num; ++i)
            for (int b = 0; b < kNumBands; ++b)
                bandGain[(size_t) b] += (targetGain[(size_t) b] - bandGain[(size_t) b]) * gainCoef;
        if (setFade < 1.0f)
            setFade = jmin (1.0f, setFade + setFadeStep * (float) num);
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
