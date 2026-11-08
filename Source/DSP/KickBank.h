#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <array>
#include <map>
#include <memory>
#include "Crossover.h"
#include "PitchDetect.h"
#include "../Parameters.h"

namespace kd
{
    //==============================================================================
    /** One band's audio after the crossover. */
    struct BandAudio
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;

        int length() const noexcept         { return audio.getNumSamples(); }
        double seconds() const noexcept     { return audio.getNumSamples() / sampleRate; }
    };

    /** Everything the audio thread plays. Immutable once published. */
    struct KickSet
    {
        std::array<BandAudio, kNumBands> bands;
        dsp::PitchResult pitch;          // of the Sub band (its own sample)
        juce::uint32 id = 0;
        bool preview = false;            // play it once as soon as the audio thread picks it up
    };

    /** Min/max peaks for drawing. */
    struct Overview
    {
        static constexpr int kNumBins = 1024;
        std::vector<float> mins, maxs;
        double seconds = 0.0;
        float peak = 0.0f;
    };

    //==============================================================================
    /**
        Holds the kick samples (one global sample, and optionally a different sample per
        band), splits them with the linear-phase crossover on a background thread and
        hands the result to the audio thread without locks or allocation:

          - every change (a sample, the crossover frequencies or slope) queues a rebuild;
            the newest request wins;
          - the loader thread decodes (with a cache), splits, detects the Sub band's pitch
            and publishes a new KickSet through an atomic pointer;
          - the audio thread calls getAudioSet() once per block, which swaps in the new set
            and queues the old one for deletion on the message thread.
    */
    class KickBank : public juce::ChangeBroadcaster,
                     private juce::Thread,
                     private juce::Timer
    {
    public:
        struct SourceInfo
        {
            juce::String path, name;      // path "" = built-in kick
            bool missing = false, error = false;
            double seconds = 0.0;
        };

        struct DisplayInfo
        {
            SourceInfo global;
            std::array<SourceInfo, kNumBands> bands;        // the sample each band actually plays
            std::array<bool, kNumBands> overridden {};       // band has its own sample
            std::array<std::shared_ptr<const Overview>, kNumBands> bandOverviews;
            std::shared_ptr<const Overview> globalOverview;
            dsp::PitchResult pitch;
            std::shared_ptr<const KickSet> set;              // a copy of what the audio thread plays (for offline renders)
            bool loading = false;
            juce::uint32 generation = 0;
        };

        static constexpr double kMaxSeconds = 30.0;
        static constexpr const char* kBuiltInName = "Built-in Kick";

        KickBank();
        ~KickBank() override;

        juce::AudioFormatManager& getFormatManager() noexcept   { return formatManager; }
        juce::String getSupportedWildcard() const               { return formatManager.getWildcardForAllFormats(); }
        bool isSupportedFile (const juce::File&) const;

        // Message thread ---------------------------------------------------------------
        /** Loads a sample into every band (bands with their own sample go back to following it). */
        void loadGlobal (const juce::File&, bool preview = false);
        /** Gives one band its own sample. */
        void loadBand (int band, const juce::File&, bool preview = false);
        /** The band follows the global sample again. */
        void clearBand (int band);
        void setCrossover (const dsp::CrossoverSettings&);
        /** Restores the samples saved with a project (missing files keep their path). */
        void restore (const juce::String& globalPath, const std::array<juce::String, kNumBands>& bandPaths);

        juce::String getGlobalPath() const;
        juce::String getBandPath (int band) const;     // "" = follows the global sample
        /** The file a band plays (its own, else the global one); empty for the built-in kick. */
        juce::File getEffectiveFile (int band) const;
        juce::File getGlobalFile() const;

        DisplayInfo getDisplayInfo() const;
        bool waitUntilIdle (int timeoutMs);

        // Audio thread -------------------------------------------------------------------
        /** The set to play this block (nullptr until the first build). Never allocates or locks. */
        const KickSet* getAudioSet() noexcept;
        /** The set that was playing before the last swap (kept so playing hits can crossfade). */
        const KickSet* getPreviousSet() const noexcept   { return previous; }
        /** The crossfade is over: the previous set can be deleted. */
        void releasePrevious() noexcept;

        // Exposed for tests -----------------------------------------------------------------
        static std::unique_ptr<BandAudio> makeBuiltInKick();
        static std::shared_ptr<const Overview> makeOverview (const juce::AudioBuffer<float>&, double sampleRate);
        /** De-clicks a band's edges: a short fade-in, and the end trimmed to its last zero crossing
            and faded out, so a band never starts or stops with a step. */
        static void declickEdges (juce::AudioBuffer<float>&, double sampleRate);

    private:
        struct Request
        {
            juce::String globalPath;
            std::array<juce::String, kNumBands> bandPaths;
            dsp::CrossoverSettings crossover;
            bool preview = false;
            juce::uint32 generation = 0;
        };

        struct Decoded
        {
            BandAudio audio;
            SourceInfo info;
        };

        void run() override;
        void timerCallback() override;
        void requestRebuild (std::function<void (Request&)> change);
        std::shared_ptr<const Decoded> decode (const juce::String& path);
        void publish (std::unique_ptr<KickSet>);
        void collectGarbage();

        juce::AudioFormatManager formatManager;

        mutable juce::CriticalSection requestLock;
        Request request;
        juce::uint32 builtGeneration = 0;
        std::atomic<bool> busy { false };

        std::map<juce::String, std::shared_ptr<const Decoded>> cache;   // loader thread only

        mutable juce::CriticalSection displayLock;
        DisplayInfo display;

        // audio hand-off
        std::atomic<KickSet*> pending { nullptr };
        KickSet* current = nullptr;                // audio thread
        KickSet* previous = nullptr;               // audio thread
        void retire (KickSet*) noexcept;
        juce::AbstractFifo retiredFifo { 64 };
        std::array<KickSet*, 64> retired {};
        juce::uint32 nextSetId = 1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickBank)
    };
}
