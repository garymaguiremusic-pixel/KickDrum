#pragma once

#include <juce_events/juce_events.h>
#include "KickEngine.h"
#include "FxChain.h"

namespace kd
{
    /** The level of one band on its own, as heard: after its envelope, Fade and Level knob
        (before the FX and the master volume, which act on all bands together). */
    struct BandLevel
    {
        bool active = false;          // false when the band is muted / not soloed
        float peakDb = -100.0f;       // sample peak
        float rmsDb = -100.0f;        // loudest 50 ms RMS: the band's weight
    };

    /** Measures one band's peak and loudest 50 ms RMS (exposed for tests). */
    BandLevel measureLevel (const juce::AudioBuffer<float>&, double sampleRate);

    /** One hit rendered through the whole instrument: bands, tail, FX and volume. */
    struct RenderedHit
    {
        std::array<BandLevel, kNumBands> bands;   // filled when the request asks for band levels
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
            bool measureBands = false;      // also render each band on its own and measure it
        };

        OutputRenderer();
        ~OutputRenderer() override;

        void request (const Request&);
        /** Called on the message thread with each finished render. */
        std::function<void (std::shared_ptr<const RenderedHit>)> onRendered;

        /** Synchronous render (used by the thread, the audio export, and tests). */
        static std::shared_ptr<RenderedHit> render (const Request&);

        /** Writes a hit as a 24-bit WAV: from the hit (time 0) to where it ends (-60 dBFS),
            plus a 10 ms fade so the file never ends with a click. Returns false on failure. */
        static bool writeWav (const RenderedHit&, const juce::File&);

    private:
        void run() override;

        juce::CriticalSection lock;
        std::unique_ptr<Request> pending;
        juce::uint32 nextId = 1;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
