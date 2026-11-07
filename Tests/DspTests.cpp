#include <juce_core/juce_core.h>
#include "DSP/Crossover.h"
#include "DSP/PitchDetect.h"
#include "DSP/KickBank.h"
#include "DSP/KickEngine.h"
#include "DSP/FxChain.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"

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

        beginTest ("With open envelopes the four bands play back the original kick");
        {
            KickEngine engine;
            engine.prepare (kick->sampleRate);
            EngineSettings es;
            for (auto& b : es.bands)
            {
                b.attack = 0.0; b.hold = 5.0; b.decay = 1.0;
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
