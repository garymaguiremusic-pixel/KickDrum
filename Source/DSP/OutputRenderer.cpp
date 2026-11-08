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
