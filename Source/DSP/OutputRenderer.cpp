#include "OutputRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace juce;

namespace kd
{
    OutputRenderer::OutputRenderer() : Thread ("KickDrum output render")
    {
        startThread (Thread::Priority::low);
    }

    OutputRenderer::~OutputRenderer()
    {
        *alive = false;
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    void OutputRenderer::request (const Request& r)
    {
        {
            const ScopedLock sl (lock);
            pending = std::make_unique<Request> (r);
        }
        notify();
    }

    BandLevel measureLevel (const AudioBuffer<float>& audio, double sampleRate)
    {
        BandLevel level;
        level.active = true;
        const int len = audio.getNumSamples();
        const int chans = audio.getNumChannels();
        if (len == 0 || chans == 0)
            return level;

        const int window = jmax (1, (int) (sampleRate * 0.05));
        double sum = 0.0, best = 0.0;
        float peak = 0.0f;
        std::vector<double> sq ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            double s = 0.0;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float v = audio.getSample (ch, i);
                peak = jmax (peak, std::abs (v));
                s += (double) v * v;
            }
            sq[(size_t) i] = s / chans;
            sum += sq[(size_t) i];
            if (i >= window)
                sum -= sq[(size_t) (i - window)];
            best = jmax (best, sum / window);     // silence before the hit counts as 0
        }
        level.peakDb = Decibels::gainToDecibels (peak, -100.0f);
        level.rmsDb = best > 0.0 ? jmax (-100.0f, (float) (10.0 * std::log10 (best))) : -100.0f;
        return level;
    }

    std::shared_ptr<RenderedHit> OutputRenderer::render (const Request& r)
    {
        auto hit = std::make_shared<RenderedHit>();
        hit->sampleRate = r.sampleRate;
        const int length = jmax (1, (int) (r.seconds * r.sampleRate));
        hit->audio.setSize (2, length);
        hit->audio.clear();
        if (r.set == nullptr)
            return hit;

        constexpr int block = 512;
        KickEngine engine;
        engine.prepare (r.sampleRate);
        FxChain fx;
        fx.prepare (r.sampleRate, block, 2);
        const int latency = fx.getLatencySamples();

        auto settings = r.engine;
        settings.velocitySensitivity = 0.0f;    // a full-level hit
        engine.trigger (r.set.get(), settings, 1.0f);

        // render 'latency' extra samples and drop them, so time 0 is the hit
        AudioBuffer<float> work (2, block);
        int written = -latency;
        while (written < length)
        {
            work.clear();
            engine.render (r.set.get(), settings, work, 0, block);
            fx.process (work, r.saturation, r.compressor);

            for (int i = 0; i < block; ++i, ++written)
                if (written >= 0 && written < length)
                    for (int ch = 0; ch < 2; ++ch)
                        hit->audio.setSample (ch, written, work.getSample (ch, i) * r.volume);
        }

        // peak, and the last moment above -60 dBFS
        const float floor = Decibels::decibelsToGain (-60.0f);
        int last = 0;
        for (int i = 0; i < length; ++i)
        {
            const float v = jmax (std::abs (hit->audio.getSample (0, i)), std::abs (hit->audio.getSample (1, i)));
            hit->peak = jmax (hit->peak, v);
            if (v > floor)
                last = i;
        }
        hit->endSeconds = last / r.sampleRate;

        // each band on its own, as heard (envelope, Fade, Level), without the FX and master volume
        if (r.measureBands)
        {
            for (int b = 0; b < kNumBands; ++b)
            {
                auto& level = hit->bands[(size_t) b];
                if (! settings.bands[(size_t) b].enabled)
                {
                    level = BandLevel();
                    continue;
                }

                KickEngine solo;
                solo.prepare (r.sampleRate);
                solo.trigger (r.set.get(), settings, 1.0f, (uint8) (1 << b));
                AudioBuffer<float> band (2, length);
                band.clear();
                for (int pos = 0; pos < length; pos += block)
                    solo.render (r.set.get(), settings, band, pos, jmin (block, length - pos));
                level = measureLevel (band, r.sampleRate);
            }
        }
        return hit;
    }

    bool OutputRenderer::writeWav (const RenderedHit& hit, const File& file)
    {
        const int total = hit.audio.getNumSamples();
        const int fade = (int) (hit.sampleRate * 0.01);
        const int length = jlimit (1, total, (int) (hit.endSeconds * hit.sampleRate) + fade);

        AudioBuffer<float> out (2, length);
        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, 0, hit.audio, jmin (ch, hit.audio.getNumChannels() - 1), 0, length);
            const int f = jmin (fade, length);
            for (int i = 0; i < f; ++i)   // raised-cosine fade to exactly 0
                out.setSample (ch, length - f + i,
                               out.getSample (ch, length - f + i) * (0.5f + 0.5f * std::cos (MathConstants<float>::pi * (float) (i + 1) / (float) f)));
        }

        file.getParentDirectory().createDirectory();
        file.deleteFile();
        std::unique_ptr<OutputStream> stream = file.createOutputStream();
        if (stream == nullptr)
            return false;

        WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (hit.sampleRate)
                                                                           .withNumChannels (2)
                                                                           .withBitsPerSample (24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer (out, 0, length);
    }

    void OutputRenderer::run()
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<Request> r;
            {
                const ScopedLock sl (lock);
                r = std::move (pending);
            }
            if (r == nullptr)
            {
                wait (-1);
                continue;
            }

            auto hit = render (*r);
            hit->id = nextId++;

            MessageManager::callAsync ([this, alive = alive, hit = std::shared_ptr<const RenderedHit> (hit)]
            {
                if (*alive && onRendered)
                    onRendered (hit);
            });
        }
    }
}
