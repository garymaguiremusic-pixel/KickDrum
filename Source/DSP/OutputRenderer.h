#pragma once

#include <juce_events/juce_events.h>
#include "KickEngine.h"
#include "FxChain.h"

namespace kd
{
    /** One hit rendered through the whole instrument: bands, tail, FX and volume. */
    struct RenderedHit
    {
        juce::AudioBuffer<float> audio;     // stereo, at 'sampleRate'
        double sampleRate = 48000.0;
        double endSeconds = 0.0;            // where it falls below -60 dBFS for good
        float peak = 0.0f;
        juce::uint32 id = 0;
    };

    /**
        Renders a single full-velocity hit offline on a background thread, so the output
        display shows the result of every edit straight away, without playing a note.
        The newest request wins.
    */
    class OutputRenderer : private juce::Thread
    {
    public:
        struct Request
        {
            std::shared_ptr<const KickSet> set;
            EngineSettings engine;
            SaturationSettings saturation;
            CompressorSettings compressor;
            float volume = 1.0f;
            double sampleRate = 48000.0;
            double seconds = 2.0;
        };

        OutputRenderer();
        ~OutputRenderer() override;

        void request (const Request&);
        /** Called on the message thread with each finished render. */
        std::function<void (std::shared_ptr<const RenderedHit>)> onRendered;

        /** Synchronous render (used by the thread, and by tests). */
        static std::shared_ptr<RenderedHit> render (const Request&);

    private:
        void run() override;

        juce::CriticalSection lock;
        std::unique_ptr<Request> pending;
        juce::uint32 nextId = 1;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
