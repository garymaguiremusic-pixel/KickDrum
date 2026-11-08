#include <juce_core/juce_core.h>
#include "DSP/Crossover.h"
#include "DSP/PitchDetect.h"
#include "DSP/KickBank.h"
#include "DSP/KickEngine.h"
#include "DSP/FxChain.h"
#include "DSP/OutputRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/Shared/VirusComponents.h"

using namespace juce;
using namespace kd;

namespace
{
    AudioBuffer<float> makeNoise (int channels, int length, int64 seed)
    {
        AudioBuffer<float> b (channels, length);
        Random rng (seed);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        return b;
    }

    float maxDifference (const AudioBuffer<float>& a, const AudioBuffer<float>& b, int ch, int offsetB = 0)
    {
        float m = 0.0f;
        for (int i = 0; i < a.getNumSamples() && i + offsetB < b.getNumSamples(); ++i)
            m = jmax (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i + offsetB)));
        return m;
    }
}

//==============================================================================
class CrossoverTests : public UnitTest
{
public:
    CrossoverTests() : UnitTest ("Crossover", "DSP") {}

    void runTest() override
    {
        beginTest ("Band gains add up to exactly 1 at every frequency, for every slope");
        for (int slope = 0; slope < 5; ++slope)
        {
            kd::dsp::CrossoverSettings s;
            s.slopeIndex = slope;
            float worst = 0.0f;
            for (double f = 0.0; f < 24000.0; f += 7.3)
            {
                float sum = 0.0f;
                for (int b = 0; b < 4; ++b)
                    sum += kd::dsp::bandGain (b, f, s);
                worst = jmax (worst, std::abs (sum - 1.0f));
            }
            expectLessThan (worst, 1.0e-5f);
        }

        beginTest ("Bands are -6 dB at the crossover and fall at the chosen slope");
        {
            kd::dsp::CrossoverSettings s;
            s.slopeIndex = 3;   // 24 dB/oct
            expectWithinAbsoluteError (kd::dsp::bandGain (0, 120.0, s), 0.5f, 0.01f);
            // two octaves above the first crossover the Sub band is down ~48 dB
            const float db = Decibels::gainToDecibels (kd::dsp::bandGain (0, 480.0, s));
            expectWithinAbsoluteError (db, -48.0f, 1.0f);
        }

        beginTest ("The four bands add back to the original (noise and the built-in kick)");
        for (int slope = 0; slope < 5; ++slope)
        {
            kd::dsp::CrossoverSettings s;
            s.slopeIndex = slope;
            const auto src = makeNoise (2, 20000, 7 + slope);
            std::array<AudioBuffer<float>, 4> bands;
            kd::dsp::splitIntoBands (src, 48000.0, s, { true, true, true, true }, bands);

            for (int ch = 0; ch < 2; ++ch)
            {
                AudioBuffer<float> sum (2, src.getNumSamples());
                sum.clear();
                for (auto& b : bands)
                    sum.addFrom (ch, 0, b, ch, 0, src.getNumSamples());
                expectLessThan (maxDifference (src, sum, ch), 1.0e-4f);
            }
        }
        {
            auto kick = KickBank::makeBuiltInKick();
            std::array<AudioBuffer<float>, 4> bands;
            kd::dsp::splitIntoBands (kick->audio, kick->sampleRate, {}, { true, true, true, true }, bands);
            AudioBuffer<float> sum (1, kick->length());
            sum.clear();
            for (auto& b : bands)
                sum.addFrom (0, 0, b, 0, 0, kick->length());
            expectLessThan (maxDifference (kick->audio, sum, 0), 1.0e-4f);
        }

        beginTest ("Zero phase: an impulse stays centred (symmetric response, no delay)");
        {
            AudioBuffer<float> imp (1, 8001);
            imp.clear();
            imp.setSample (0, 4000, 1.0f);
            std::array<AudioBuffer<float>, 4> bands;
            kd::dsp::splitIntoBands (imp, 48000.0, {}, { true, true, true, true }, bands);
            for (int b = 0; b < 4; ++b)
            {
                float asym = 0.0f;
                int peakAt = 0;
                for (int i = 1; i < 4000; ++i)
                    asym = jmax (asym, std::abs (bands[(size_t) b].getSample (0, 4000 - i) - bands[(size_t) b].getSample (0, 4000 + i)));
                for (int i = 0; i < 8001; ++i)
                    if (std::abs (bands[(size_t) b].getSample (0, i)) > std::abs (bands[(size_t) b].getSample (0, peakAt)))
                        peakAt = i;
                expectLessThan (asym, 1.0e-5f, "band " + String (b) + " is not symmetric");
                expectEquals (peakAt, 4000);
            }
        }

        beginTest ("Crossover frequencies are kept in order");
        {
            kd::dsp::CrossoverSettings s;
            s.freqs = { 400.0f, 300.0f, 200.0f };
            const auto c = s.sanitised (44100.0);
            expect (c.freqs[0] < c.freqs[1] && c.freqs[1] < c.freqs[2]);
        }
    }
};

static CrossoverTests crossoverTests;

//==============================================================================
class PitchTests : public UnitTest
{
public:
    PitchTests() : UnitTest ("Pitch", "DSP") {}

    void runTest() override
    {
        beginTest ("Detects the built-in kick's 50 Hz body");
        {
            auto kick = KickBank::makeBuiltInKick();
            std::array<AudioBuffer<float>, 4> bands;
            kd::dsp::splitIntoBands (kick->audio, kick->sampleRate, {}, { true, false, false, false }, bands);
            const auto p = kd::dsp::detectKickPitch (bands[0].getReadPointer (0), bands[0].getNumSamples(), kick->sampleRate);
            expect (p.isValid());
            expectWithinAbsoluteError (p.hz, 50.0, 2.0);
        }

        beginTest ("Detects decaying sines from 35 to 110 Hz");
        for (double hz : { 35.0, 41.2, 55.0, 73.4, 110.0 })
        {
            const double sr = 44100.0;
            std::vector<float> x ((size_t) (sr * 0.6));
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (std::sin (MathConstants<double>::twoPi * hz * (double) i / sr) * std::exp (-(double) i / sr / 0.3));
            const auto p = kd::dsp::detectKickPitch (x.data(), (int) x.size(), sr);
            expectWithinAbsoluteError (p.hz, hz, hz * 0.01);
        }

        beginTest ("Measures a sinusoid's amplitude and phase");
        {
            const double sr = 48000.0, hz = 60.0, amp = 0.7, phase = 1.1;
            std::vector<float> x (20000);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (amp * std::cos (MathConstants<double>::twoPi * hz * ((double) i - 10000.5) / sr + phase));
            float a = 0.0f;
            double ph = 0.0;
            kd::dsp::measureSinusoid (x.data(), (int) x.size(), 10000.5, hz, sr, a, ph);
            expectWithinAbsoluteError ((double) a, amp, 0.01);
            expectWithinAbsoluteError (ph, phase, 0.01);
        }
    }
};

static PitchTests pitchTests;

//==============================================================================
class EngineTests : public UnitTest
{
public:
    EngineTests() : UnitTest ("Engine", "DSP") {}

    static KickSet makeSet (const BandAudio& src, std::array<bool, 4> wanted = { true, true, true, true })
    {
        KickSet set;
        std::array<AudioBuffer<float>, 4> outs;
        kd::dsp::splitIntoBands (src.audio, src.sampleRate, {}, wanted, outs);
        for (int b = 0; b < 4; ++b)
        {
            set.bands[(size_t) b].audio = outs[(size_t) b];
            set.bands[(size_t) b].sampleRate = src.sampleRate;
        }
        std::vector<float> mono (outs[0].getReadPointer (0), outs[0].getReadPointer (0) + outs[0].getNumSamples());
        set.pitch = kd::dsp::detectKickPitch (mono.data(), (int) mono.size(), src.sampleRate);
        return set;
    }

    void runTest() override
    {
        auto kick = KickBank::makeBuiltInKick();
        const auto set = makeSet (*kick);

        beginTest ("Envelope: attack, hold, decay");
        {
            BandSettings s;
            s.attack = 0.01; s.hold = 0.02; s.decay = 0.1; s.curve = 0.0f;
            expectWithinAbsoluteError (envelopeAt (s, 0.005), 0.5f, 1.0e-4f);
            expectEquals (envelopeAt (s, 0.02), 1.0f);
            expectWithinAbsoluteError (envelopeAt (s, 0.08), 0.5f, 1.0e-4f);
            expectEquals (envelopeAt (s, 0.2), 0.0f);
        }

        beginTest ("Decay curves start at 1, end softly at 0 and never jump");
        for (float curve : { -1.0f, -0.5f, 0.0f, 0.4f, 1.0f })
        {
            expectWithinAbsoluteError (decayShape (0.0, curve), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (decayShape (1.0, curve), 0.0f, 1.0e-6f);
            float prev = 1.0f, maxStep = 0.0f;
            for (int i = 1; i <= 1000; ++i)
            {
                const float v = decayShape (i / 1000.0, curve);
                expectLessOrEqual (v, prev + 1.0e-6f);
                maxStep = jmax (maxStep, prev - v);
                prev = v;
            }
            expectLessThan (maxStep, 0.01f, "curve " + String (curve) + " drops too suddenly");
        }
        // positive = exponential: already well down at the half-way point; negative = still high
        expectLessThan (decayShape (0.5, 1.0f), 0.05f);
        expectGreaterThan (decayShape (0.5, -1.0f), 0.95f);

        beginTest ("With open envelopes the four bands play back the original kick");
        {
            KickEngine engine;
            engine.prepare (kick->sampleRate);
            EngineSettings es;
            for (auto& b : es.bands)
            {
                b.attack = 0.0; b.hold = 5.0; b.decay = 1.0; b.fade = 0.0;
            }
            es.velocitySensitivity = 0.0f;
            engine.trigger (&set, es, 1.0f);

            AudioBuffer<float> out (2, kick->length());
            out.clear();
            for (int pos = 0; pos < out.getNumSamples(); pos += 512)
                engine.render (&set, es, out, pos, jmin (512, out.getNumSamples() - pos));
            expectLessThan (maxDifference (kick->audio, out, 0), 1.0e-4f);
        }

        beginTest ("Sync length: a band stops at the note length");
        {
            KickEngine engine;
            engine.prepare (48000.0);
            EngineSettings es;
            for (auto& b : es.bands)
            {
                b.attack = 0.0; b.hold = 0.05; b.decay = 0.05;     // 100 ms
            }
            engine.trigger (&set, es, 1.0f);
            AudioBuffer<float> out (2, 9600);
            out.clear();
            engine.render (&set, es, out, 0, out.getNumSamples());
            expect (out.getMagnitude (0, 4900, 4700) == 0.0f, "audio after the band's end");
            expect (! engine.isPlaying());
        }

        beginTest ("Tail continues past the sample in the key");
        {
            KickEngine engine;
            engine.prepare (48000.0);
            EngineSettings es;
            for (auto& b : es.bands)
            {
                b.attack = 0.0; b.hold = 1.6; b.decay = 0.4;     // 2 s: longer than the 0.8 s kick
            }
            es.tail.on = true;
            es.tail.keyHz = 55.0;
            es.tail.start = 0.12;
            es.tail.glide = 0.05;
            engine.trigger (&set, es, 1.0f);

            AudioBuffer<float> out (2, 96000);
            out.clear();
            for (int pos = 0; pos < out.getNumSamples(); pos += 256)
                engine.render (&set, es, out, pos, jmin (256, out.getNumSamples() - pos));

            // still sounding at 1.2 s (the sample itself ended at 0.8 s)
            expectGreaterThan (out.getMagnitude (0, 57600, 2400), 0.01f);

            // and at the key: count zero crossings over 0.5 s
            int crossings = 0;
            for (int i = 48000; i < 72000; ++i)
                if ((out.getSample (0, i - 1) < 0.0f) != (out.getSample (0, i) < 0.0f))
                    ++crossings;
            expectWithinAbsoluteError ((double) crossings / 2.0 / 0.5, 55.0, 2.0);

            // no click at the hand-over: the level stays smooth around the tail start
            float maxStep = 0.0f;
            for (int i = 4800; i < 7200; ++i)
                maxStep = jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            expectLessThan (maxStep, 0.03f);
        }

        beginTest ("No clicks when Length, Decay or Curve move while a note plays");
        {
            // a steady 50 Hz tone in every band makes any step in the envelope easy to see
            KickSet tone;
            for (int b = 0; b < 4; ++b)
            {
                auto& band = tone.bands[(size_t) b];
                band.sampleRate = 48000.0;
                band.audio.setSize (1, 96000);
                for (int i = 0; i < 96000; ++i)
                    band.audio.setSample (0, i, b == 0 ? 0.8f * (float) std::sin (MathConstants<double>::twoPi * 50.0 * i / 48000.0) : 0.0f);
            }

            auto maxStepWhen = [this, &tone] (std::function<void (EngineSettings&)> change)
            {
                KickEngine engine;
                engine.prepare (48000.0);
                EngineSettings es;
                for (auto& b : es.bands)
                {
                    b.attack = 0.0; b.hold = 0.6; b.decay = 0.6; b.curve = 0.4f;
                }
                engine.trigger (&tone, es, 1.0f);
                AudioBuffer<float> out (2, 48000);
                out.clear();
                for (int pos = 0; pos < 48000; pos += 64)
                {
                    if (pos == 24320)
                        change (es);                       // at ~0.5 s, mid-note, mid-cycle
                    engine.render (&tone, es, out, pos, 64);
                }
                // the tone itself moves at most 0.8 * 2 * pi * 50 / 48000 = 0.0052 per sample
                float maxStep = 0.0f;
                for (int i = 1; i < 48000; ++i)
                    maxStep = jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
                return maxStep;
            };

            expectLessThan (maxStepWhen ([] (EngineSettings& es) { es.bands[0].hold = 0.1; es.bands[0].decay = 0.1; }), 0.012f,
                            "shortening the Length below the playhead");
            expectLessThan (maxStepWhen ([] (EngineSettings& es) { es.bands[0].hold = 0.0; es.bands[0].decay = 1.2; }), 0.012f,
                            "moving the Decay");
            expectLessThan (maxStepWhen ([] (EngineSettings& es) { es.bands[0].hold = 0.4; es.bands[0].curve = 1.0f; es.bands[0].decay = 0.8; }), 0.012f,
                            "moving the Curve");
            expectLessThan (maxStepWhen ([] (EngineSettings& es) { es.bands[0].enabled = false; }), 0.012f, "muting the band");
        }

        beginTest ("Re-split bands (crossover moved mid-note) crossfade instead of jumping");
        {
            KickSet a, b;
            for (int k = 0; k < 4; ++k)
                for (auto* set : { &a, &b })
                {
                    auto& band = set->bands[(size_t) k];
                    band.sampleRate = 48000.0;
                    band.audio.setSize (1, 96000);
                    const float sign = set == &a ? 1.0f : -1.0f;      // b is a's mirror: the worst case
                    for (int i = 0; i < 96000; ++i)
                        band.audio.setSample (0, i, k == 0 ? sign * 0.8f * (float) std::sin (MathConstants<double>::twoPi * 50.0 * i / 48000.0) : 0.0f);
                }

            KickEngine engine;
            engine.prepare (48000.0);
            EngineSettings es;
            engine.trigger (&a, es, 1.0f);
            AudioBuffer<float> out (2, 24000);
            out.clear();
            for (int pos = 0; pos < 24000; pos += 64)
                engine.render (pos < 12032 ? &a : &b, es, out, pos, 64, pos < 12032 ? nullptr : &a);
            float maxStep = 0.0f;
            for (int i = 1; i < 24000; ++i)
                maxStep = jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            expectLessThan (maxStep, 0.012f);
            expect (! engine.isCrossfadingSets());
        }

        beginTest ("Band edges are faded: they start and end at exactly zero");
        {
            auto buffer = makeNoise (2, 4800, 11);
            KickBank::declickEdges (buffer, 48000.0);
            int end = buffer.getNumSamples() - 1;
            while (end > 0 && buffer.getSample (0, end) == 0.0f && buffer.getSample (1, end) == 0.0f)
                --end;
            for (int ch = 0; ch < 2; ++ch)
            {
                expectEquals (buffer.getSample (ch, 0), 0.0f);
                expectLessThan (std::abs (buffer.getSample (ch, end)), 0.01f);
            }
        }

        beginTest ("Start / End points and the band Fade: no step at either edge");
        {
            KickSet tone;
            for (int b = 0; b < 4; ++b)
            {
                auto& band = tone.bands[(size_t) b];
                band.sampleRate = 48000.0;
                band.audio.setSize (1, 48000);
                for (int i = 0; i < 48000; ++i)
                    band.audio.setSample (0, i, b == 0 ? 0.8f * (float) std::sin (MathConstants<double>::twoPi * 50.0 * i / 48000.0 + 1.0) : 0.0f);
            }
            KickEngine engine;
            engine.prepare (48000.0);
            EngineSettings es;
            for (auto& b : es.bands)
            {
                b.attack = 0.0; b.hold = 2.0; b.decay = 0.1; b.fade = 0.03;
            }
            es.startFrac = 0.2071;     // mid-cycle
            es.endFrac = 0.6033;
            engine.trigger (&tone, es, 1.0f);
            AudioBuffer<float> out (2, 48000);
            out.clear();
            engine.render (&tone, es, out, 0, out.getNumSamples());

            float maxStep = std::abs (out.getSample (0, 0));
            for (int i = 1; i < 48000; ++i)
                maxStep = jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            expectLessThan (maxStep, 0.012f);

            // it starts at the Start point and is silent after the End point (0.396 s of sample)
            const int endSample = (int) ((0.6033 - 0.2071) * 48000.0);
            expectGreaterThan (out.getMagnitude (0, 2000, 4000), 0.5f);
            expectEquals (out.getMagnitude (0, endSample + 2, 2000), 0.0f);
            // ... and fading in the last 30 ms
            expectLessThan (out.getMagnitude (0, endSample - 200, 200), 0.1f);
        }

        beginTest ("Tune Body plays the kick in the key's nearest octave");
        {
            TailSettings t;
            t.tuneBody = true;
            t.keyHz = 55.0;
            kd::dsp::PitchResult p;
            p.hz = 50.0;
            expectWithinAbsoluteError (tuneRatio (t, p), 1.1, 1.0e-9);
            t.keyHz = 110.0;
            expectWithinAbsoluteError (tuneRatio (t, p), 1.1, 1.0e-9);
        }
    }
};

static EngineTests engineTests;

//==============================================================================
class FxTests : public UnitTest
{
public:
    FxTests() : UnitTest ("FX", "DSP") {}

    void runTest() override
    {
        beginTest ("Latency is the same with saturation on (mix 0) and off");
        {
            FxChain fx;
            fx.prepare (48000.0, 512, 2);
            const int latency = fx.getLatencySamples();
            expectGreaterOrEqual (latency, 0);

            const auto src = makeNoise (2, 4096, 3);
            for (bool satOn : { false, true })
            {
                fx.reset();
                AudioBuffer<float> io (src);
                SaturationSettings sat;
                sat.on = satOn;
                sat.mix = 0.0f;
                CompressorSettings comp;
                for (int pos = 0; pos < io.getNumSamples(); pos += 512)
                {
                    AudioBuffer<float> block (io.getArrayOfWritePointers(), 2, pos, 512);
                    fx.process (block, sat, comp);
                }
                for (int ch = 0; ch < 2; ++ch)
                {
                    float m = 0.0f;
                    for (int i = 1000; i < 4000; ++i)
                        m = jmax (m, std::abs (io.getSample (ch, i) - src.getSample (ch, i - latency)));
                    expectLessThan (m, 1.0e-5f);
                }
            }
        }

        beginTest ("Saturation curves stay bounded and pass zero");
        for (int type = 0; type < 6; ++type)
        {
            expectWithinAbsoluteError (saturate (type, 0.0f), 0.0f, 1.0e-6f);
            for (float x = -50.0f; x <= 50.0f; x += 0.25f)
                expectLessThan (std::abs (saturate (type, x)), 2.0f);
        }

        beginTest ("Compressor reduces a loud signal and leaves a quiet one alone");
        {
            FxChain fx;
            fx.prepare (48000.0, 512, 2);
            CompressorSettings comp;
            comp.on = true;
            comp.thresholdDb = -20.0f;
            comp.ratio = 4.0f;
            comp.attackMs = 1.0f;
            SaturationSettings sat;

            AudioBuffer<float> loud (2, 512);
            for (int block = 0; block < 40; ++block)
            {
                for (int i = 0; i < 512; ++i)
                    loud.setSample (0, i, 0.9f * (float) std::sin (i * 0.05)), loud.setSample (1, i, loud.getSample (0, i));
                fx.process (loud, sat, comp);
            }
            expectGreaterThan (fx.getGainReductionDb(), 6.0f);
        }
    }
};

static FxTests fxTests;

//==============================================================================
class ProcessorTests : public UnitTest
{
public:
    ProcessorTests() : UnitTest ("Processor", "Plugin") {}

    void runTest() override
    {
        beginTest ("A note plays the built-in kick");
        {
            KickDrumAudioProcessor p;
            p.getBank().waitUntilIdle (5000);
            p.prepareToPlay (48000.0, 512);

            AudioBuffer<float> buffer (2, 512);
            MidiBuffer midi;
            midi.addEvent (MidiMessage::noteOn (1, 36, (uint8) 127), 0);
            float peak = 0.0f;
            for (int block = 0; block < 20; ++block)
            {
                p.processBlock (buffer, midi);
                midi.clear();
                peak = jmax (peak, buffer.getMagnitude (0, 0, 512));
            }
            expectGreaterThan (peak, 0.3f);
        }

        beginTest ("State round trip keeps parameters and samples");
        {
            MemoryBlock state;
            {
                KickDrumAudioProcessor p;
                p.apvts.getParameter (ParamIDs::xover1)->setValueNotifyingHost (
                    p.apvts.getParameter (ParamIDs::xover1)->convertTo0to1 (90.0f));
                p.getBank().loadBand (2, File ("/does/not/exist.wav"));
                p.getBank().waitUntilIdle (5000);
                p.getStateInformation (state);
            }
            KickDrumAudioProcessor q;
            q.setStateInformation (state.getData(), (int) state.getSize());
            expectWithinAbsoluteError (q.apvts.getRawParameterValue (ParamIDs::xover1)->load(), 90.0f, 0.5f);
            expectEquals (q.getBank().getBandPath (2), File ("/does/not/exist.wav").getFullPathName());
            q.getBank().waitUntilIdle (5000);
            expect (q.getBank().getDisplayInfo().bands[2].missing);
        }

        beginTest ("Loading a WAV file splits it and detects its pitch");
        {
            // write a 55 Hz kick to a temporary WAV
            TemporaryFile temp (".wav");
            {
                const double sr = 44100.0;
                AudioBuffer<float> kick (2, (int) (sr * 0.7));
                for (int i = 0; i < kick.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    const float v = (float) (0.8 * std::sin (MathConstants<double>::twoPi * 55.0 * t) * std::exp (-t / 0.25));
                    kick.setSample (0, i, v);
                    kick.setSample (1, i, v);
                }
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> stream = temp.getFile().createOutputStream();
                auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (sr)
                                                                                   .withNumChannels (2)
                                                                                   .withBitsPerSample (24));
                expect (writer != nullptr);
                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (kick, 0, kick.getNumSamples());
            }

            KickDrumAudioProcessor p;
            p.getBank().loadGlobal (temp.getFile());
            expect (p.getBank().waitUntilIdle (10000));
            const auto info = p.getBank().getDisplayInfo();
            expectEquals (info.global.name, temp.getFile().getFileNameWithoutExtension());
            expect (! info.global.missing && ! info.global.error);
            expectWithinAbsoluteError (info.pitch.hz, 55.0, 1.0);
            expect (info.bandOverviews[0] != nullptr && info.bandOverviews[0]->peak > 0.5f);
            expect (info.bandOverviews[3] != nullptr && info.bandOverviews[3]->peak < 0.05f);   // a sine has no air
        }

        beginTest ("Length sets the whole band; Decay is the fade at its end");
        {
            KickDrumAudioProcessor p;
            auto set = [&p] (const String& id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            set (bandParam (0, BandParams::attack), 10.0f);
            set (bandParam (0, BandParams::length), 400.0f);
            set (bandParam (0, BandParams::decay), 150.0f);
            auto b = p.makeEngineSettings().bands[0];
            expectWithinAbsoluteError (b.length(), 0.4, 1.0e-4);
            expectWithinAbsoluteError (b.decay, 0.15, 1.0e-4);
            expectWithinAbsoluteError (b.hold, 0.24, 1.0e-4);

            // a decay longer than the length fills it (after the attack)
            set (bandParam (0, BandParams::decay), 2000.0f);
            b = p.makeEngineSettings().bands[0];
            expectWithinAbsoluteError (b.decay, 0.39, 1.0e-4);
            expectWithinAbsoluteError (b.hold, 0.0, 1.0e-6);

            // Sync: 1/8 at 120 BPM = 250 ms in total, and the decay still fades over it
            set (bandParam (0, BandParams::decayMode), 1.0f);
            set (bandParam (0, BandParams::sync), 1.0f);
            b = p.makeEngineSettings().bands[0];
            expectWithinAbsoluteError (b.length(), 0.25, 1.0e-6);
            expectWithinAbsoluteError (b.decay, 0.24, 1.0e-4);
        }

        beginTest ("Attack can be synced (up to 1/4)");
        {
            KickDrumAudioProcessor p;
            auto set = [&p] (const String& id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            set (bandParam (1, BandParams::attackMode), 1.0f);
            set (bandParam (1, BandParams::attackSync), 2.0f);      // 1/16 at 120 BPM = 125 ms
            expectWithinAbsoluteError (p.makeEngineSettings().bands[1].attack, 0.125, 1.0e-6);
            set (bandParam (1, BandParams::attackSync), 4.0f);      // 1/4 = 500 ms
            expectWithinAbsoluteError (p.makeEngineSettings().bands[1].attack, 0.5, 1.0e-6);
        }

        beginTest ("Tail Start can be synced");
        {
            KickDrumAudioProcessor p;
            auto set = [&p] (const String& id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            set (ParamIDs::tailStartMode, 1.0f);
            set (ParamIDs::tailStartSync, 3.0f);      // 1/8 at 120 BPM = 250 ms
            expectWithinAbsoluteError (p.makeEngineSettings().tail.start, 0.25, 1.0e-6);
        }

        beginTest ("Key Lock: MIDI notes set the key, in the scale, near the Key knob");
        {
            KickDrumAudioProcessor p;
            auto set = [&p] (const String& id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            // Key knob A1 (33), A minor
            expectEquals (p.keyLockedNote (48), 36);    // C  -> C2 (3 above A1)
            expectEquals (p.keyLockedNote (50), 38);    // D  -> D2
            expectEquals (p.keyLockedNote (52), 28);    // E  -> E1 (nearer than E2)
            expectEquals (p.keyLockedNote (49), 36);    // C# isn't in A minor -> C
            expectEquals (p.keyLockedNote (33 + 24), 33);

            // the tail of a played note sounds at that note
            set (ParamIDs::keyLock, 1.0f);
            set (ParamIDs::tailOn, 1.0f);
            set (bandParam (0, BandParams::length), 2000.0f);
            set (bandParam (0, BandParams::decay), 300.0f);
            p.getBank().waitUntilIdle (5000);
            p.prepareToPlay (48000.0, 512);

            AudioBuffer<float> out (2, 48000 * 2);
            out.clear();
            AudioBuffer<float> block (2, 512);
            for (int pos = 0; pos < out.getNumSamples(); pos += 512)
            {
                MidiBuffer midi;
                if (pos == 0)
                    midi.addEvent (MidiMessage::noteOn (1, 50, (uint8) 127), 0);    // D -> D2, 73.4 Hz
                p.processBlock (block, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, pos, block, ch, 0, jmin (512, out.getNumSamples() - pos));
            }
            int crossings = 0;
            for (int i = 48000; i < 72000; ++i)
                if ((out.getSample (0, i - 1) < 0.0f) != (out.getSample (0, i) < 0.0f))
                    ++crossings;
            expectWithinAbsoluteError ((double) crossings / 2.0 / 0.5, noteToHz (38), 2.0);
            expectEquals (p.getLastLockedNote(), 38);
        }

        beginTest ("Typed knob values are understood");
        {
            expectWithinAbsoluteError (parseValueText ("120", Unit::Ms), 120.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("120 ms", Unit::Ms), 120.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("1.5 s", Unit::Ms), 1500.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("2k", Unit::Hz), 2000.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("10 kHz", Unit::Hz), 10000.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("-3.5 dB", Unit::Db), -3.5, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("-inf", Unit::Db), -60.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("4:1", Unit::Ratio), 4.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("Exp 40", Unit::Curve), 40.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("slow 30", Unit::Curve), -30.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("-25", Unit::Curve), -25.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("Linear", Unit::Curve), 0.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("A1", Unit::Note), 33.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("F#2", Unit::Note), 42.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("Bb1", Unit::Note), 34.0, 1.0e-9);
            expectWithinAbsoluteError (parseValueText ("36", Unit::Note), 36.0, 1.0e-9);
            expect (std::isnan (parseValueText ("abc", Unit::Ms)));
        }

        beginTest ("The output render matches the instrument and finds where the hit ends");
        {
            KickDrumAudioProcessor p;
            p.getBank().waitUntilIdle (5000);
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            const auto info = p.getBank().getDisplayInfo();
            expect (info.set != nullptr);

            OutputRenderer::Request r;
            r.set = info.set;
            r.engine = p.makeEngineSettings();
            r.saturation = p.makeSaturationSettings();
            r.compressor = p.makeCompressorSettings();
            r.volume = p.getVolumeGain();
            r.sampleRate = 48000.0;
            r.seconds = 2.0;
            const auto hit = OutputRenderer::render (r);
            expectEquals (hit->audio.getNumSamples(), 96000);
            expectGreaterThan (hit->peak, 0.3f);
            // the default envelopes end at 0.9 s, so the hit is over before 0.95 s
            expectGreaterThan (hit->endSeconds, 0.3);
            expectLessThan (hit->endSeconds, 0.95);

            // saturation on: the rendered hit starts at time 0 even with the oversampling latency
            r.saturation.on = true;
            r.saturation.mix = 0.0f;
            const auto delayed = OutputRenderer::render (r);
            float diff = 0.0f;
            for (int i = 0; i < 48000; ++i)
                diff = jmax (diff, std::abs (delayed->audio.getSample (0, i) - hit->audio.getSample (0, i)));
            expectLessThan (diff, 1.0e-4f);
        }

        beginTest ("Double-click a knob and type a value");
        {
            KickDrumAudioProcessor p;
            std::unique_ptr<AudioProcessorEditor> editor (p.createEditor());

            std::function<VirusKnob* (Component&)> findKnob = [&] (Component& c) -> VirusKnob*
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* k = dynamic_cast<VirusKnob*> (child); k != nullptr && k->getName() == "Length")
                        return k;
                    if (auto* k = findKnob (*child))
                        return k;
                }
                return nullptr;
            };
            auto* knob = findKnob (*editor);
            expect (knob != nullptr);
            if (knob != nullptr)
            {
                knob->showValueEditor();
                TextEditor* ed = nullptr;
                for (auto* child : knob->getParentComponent()->getChildren())
                    if (auto* t = dynamic_cast<TextEditor*> (child))
                        ed = t;
                expect (ed != nullptr);
                if (ed != nullptr)
                {
                    ed->setText ("1.25 s");
                    ed->onReturnKey();
                    expectWithinAbsoluteError (p.apvts.getRawParameterValue (bandParam (0, BandParams::length))->load(), 1250.0f, 0.5f);
                }
            }
            MessageManager::getInstance()->runDispatchLoopUntil (50);
        }

        beginTest ("Drag Audio Out: the WAV is the rendered hit, from the beat to its end");
        {
            KickDrumAudioProcessor p;
            p.getBank().waitUntilIdle (5000);
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            const auto info = p.getBank().getDisplayInfo();
            OutputRenderer::Request r;
            r.set = info.set;
            r.engine = p.makeEngineSettings();
            r.saturation = p.makeSaturationSettings();
            r.compressor = p.makeCompressorSettings();
            r.volume = p.getVolumeGain();
            r.sampleRate = 48000.0;
            r.seconds = 2.0;
            const auto hit = OutputRenderer::render (r);

            TemporaryFile temp (".wav");
            expect (OutputRenderer::writeWav (*hit, temp.getFile()));

            AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (temp.getFile()));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                expectEquals ((int) reader->numChannels, 2);
                expectEquals (reader->sampleRate, 48000.0);
                const int expectedLength = (int) (hit->endSeconds * 48000.0) + 480;
                expectEquals ((int) reader->lengthInSamples, expectedLength);

                AudioBuffer<float> back (2, (int) reader->lengthInSamples);
                reader->read (&back, 0, back.getNumSamples(), 0, true, true);
                // same audio as the render (24-bit), starting at the hit
                float diff = 0.0f;
                for (int i = 0; i < back.getNumSamples() - 480; ++i)
                    diff = jmax (diff, std::abs (back.getSample (0, i) - hit->audio.getSample (0, i)));
                expectLessThan (diff, 1.0e-4f);
                expectLessThan (std::abs (back.getSample (0, back.getNumSamples() - 1)), 1.0e-4f);
            }
        }

        beginTest ("Every factory preset loads");
        {
            KickDrumAudioProcessor p;
            for (int i = 0; i < p.getNumPrograms(); ++i)
            {
                p.setCurrentProgram (i);
                expectEquals (p.getCurrentProgram(), i);
            }
        }
    }
};

static ProcessorTests processorTests;
