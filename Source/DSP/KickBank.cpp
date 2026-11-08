#include "KickBank.h"
#include <set>

using namespace juce;

namespace kd
{
    KickBank::KickBank() : Thread ("KickDrum sample loader")
    {
        formatManager.registerBasicFormats();
        startThread (Thread::Priority::low);
        startTimer (250);
        requestRebuild ([] (Request&) {});     // built-in kick until something is loaded
    }

    KickBank::~KickBank()
    {
        stopTimer();
        signalThreadShouldExit();
        notify();
        stopThread (5000);

        delete pending.exchange (nullptr);
        delete current;
        current = nullptr;
        collectGarbage();
    }

    bool KickBank::isSupportedFile (const File& f) const
    {
        return f.existsAsFile() && formatManager.findFormatForFileExtension (f.getFileExtension()) != nullptr;
    }

    //==============================================================================
    void KickBank::loadGlobal (const File& file, bool preview)
    {
        requestRebuild ([path = file.getFullPathName(), preview] (Request& r)
        {
            r.globalPath = path;
            for (auto& b : r.bandPaths)
                b.clear();
            r.preview = preview;
        });
    }

    void KickBank::loadBand (int band, const File& file, bool preview)
    {
        if (! isPositiveAndBelow (band, kNumBands))
            return;
        requestRebuild ([band, path = file.getFullPathName(), preview] (Request& r)
        {
            r.bandPaths[(size_t) band] = path;
            r.preview = preview;
        });
    }

    void KickBank::clearBand (int band)
    {
        if (! isPositiveAndBelow (band, kNumBands))
            return;
        requestRebuild ([band] (Request& r) { r.bandPaths[(size_t) band].clear(); });
    }

    void KickBank::setCrossover (const dsp::CrossoverSettings& s)
    {
        {
            const ScopedLock sl (requestLock);
            if (request.crossover == s)
                return;
        }
        requestRebuild ([s] (Request& r) { r.crossover = s; });
    }

    void KickBank::restore (const String& globalPath, const std::array<String, kNumBands>& bandPaths)
    {
        requestRebuild ([globalPath, bandPaths] (Request& r)
        {
            r.globalPath = globalPath;
            r.bandPaths = bandPaths;
            r.preview = false;
        });
    }

    String KickBank::getGlobalPath() const
    {
        const ScopedLock sl (requestLock);
        return request.globalPath;
    }

    String KickBank::getBandPath (int band) const
    {
        const ScopedLock sl (requestLock);
        return isPositiveAndBelow (band, kNumBands) ? request.bandPaths[(size_t) band] : String();
    }

    File KickBank::getGlobalFile() const
    {
        const auto path = getGlobalPath();
        return File::isAbsolutePath (path) ? File (path) : File();
    }

    File KickBank::getEffectiveFile (int band) const
    {
        const auto own = getBandPath (band);
        if (own.isNotEmpty())
            return File::isAbsolutePath (own) ? File (own) : File();
        return getGlobalFile();
    }

    KickBank::DisplayInfo KickBank::getDisplayInfo() const
    {
        const ScopedLock sl (displayLock);
        return display;
    }

    bool KickBank::waitUntilIdle (int timeoutMs)
    {
        const auto end = Time::getMillisecondCounter() + (uint32) timeoutMs;
        while (busy.load())
        {
            if (Time::getMillisecondCounter() > end)
                return false;
            Thread::sleep (2);
        }
        return true;
    }

    void KickBank::requestRebuild (std::function<void (Request&)> change)
    {
        {
            const ScopedLock sl (requestLock);
            change (request);
            ++request.generation;
            busy = true;
        }
        {
            const ScopedLock sl (displayLock);
            display.loading = true;
        }
        sendChangeMessage();
        notify();
    }

    //==============================================================================
    std::unique_ptr<BandAudio> KickBank::makeBuiltInKick()
    {
        // A clean sine kick: fast pitch drop from ~230 Hz to 50 Hz, a short click and a
        // smooth decay, so the plugin makes a sound before any sample is loaded.
        auto kick = std::make_unique<BandAudio>();
        kick->sampleRate = 48000.0;
        const int len = (int) (kick->sampleRate * 0.8);
        kick->audio.setSize (1, len);
        auto* d = kick->audio.getWritePointer (0);

        double phase = 0.0;
        Random rng (0x6b69636b);
        float noiseLp = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const double t = i / kick->sampleRate;
            const double f = 50.0 + 180.0 * std::exp (-t / 0.028);
            phase += MathConstants<double>::twoPi * f / kick->sampleRate;
            const double body = std::sin (phase) * std::exp (-t / 0.32) * std::min (1.0, t / 0.0006);
            noiseLp += 0.35f * ((rng.nextFloat() * 2.0f - 1.0f) - noiseLp);
            const double click = noiseLp * std::exp (-t / 0.0025) * 0.5;
            d[i] = (float) (0.89 * body + click);
        }
        // short fade at the end
        const int fade = (int) (kick->sampleRate * 0.02);
        kick->audio.applyGainRamp (0, len - fade, fade, 1.0f, 0.0f);
        return kick;
    }

    std::shared_ptr<const KickBank::Decoded> KickBank::decode (const String& path)
    {
        if (auto it = cache.find (path); it != cache.end())
            return it->second;

        auto decoded = std::make_shared<Decoded>();
        decoded->info.path = path;

        if (path.isEmpty())
        {
            decoded->audio = std::move (*makeBuiltInKick());
            decoded->info.name = kBuiltInName;
        }
        else
        {
            const File file (File::isAbsolutePath (path) ? File (path) : File());
            decoded->info.name = File::isAbsolutePath (path) ? file.getFileNameWithoutExtension() : path;

            std::unique_ptr<AudioFormatReader> reader;
            if (file.existsAsFile())
                reader.reset (formatManager.createReaderFor (file));

            if (! file.existsAsFile())
            {
                decoded->info.missing = true;
            }
            else if (reader == nullptr || reader->sampleRate <= 0.0)
            {
                decoded->info.error = true;
            }
            else
            {
                const int channels = jlimit (1, 2, (int) reader->numChannels);
                const int length = (int) jmin (reader->lengthInSamples, (int64) (reader->sampleRate * kMaxSeconds));
                decoded->audio.sampleRate = reader->sampleRate;
                decoded->audio.audio.setSize (channels, jmax (1, length));
                decoded->audio.audio.clear();
                reader->read (&decoded->audio.audio, 0, length, 0, true, channels > 1);
            }

            if (decoded->info.missing || decoded->info.error)
            {
                decoded->audio.audio.setSize (1, 1);
                decoded->audio.audio.clear();
            }
        }

        decoded->info.seconds = decoded->audio.seconds();
        cache[path] = decoded;
        return decoded;
    }

    std::shared_ptr<const Overview> KickBank::makeOverview (const AudioBuffer<float>& buffer, double sampleRate)
    {
        auto ov = std::make_shared<Overview>();
        ov->mins.assign (Overview::kNumBins, 0.0f);
        ov->maxs.assign (Overview::kNumBins, 0.0f);
        const int len = buffer.getNumSamples();
        const int chans = buffer.getNumChannels();
        ov->seconds = len / sampleRate;
        if (len <= 0 || chans <= 0)
            return ov;

        for (int bin = 0; bin < Overview::kNumBins; ++bin)
        {
            const int s0 = (int) ((int64) bin * len / Overview::kNumBins);
            const int s1 = jmax (s0 + 1, (int) ((int64) (bin + 1) * len / Overview::kNumBins));
            float lo = 0.0f, hi = 0.0f;
            for (int i = s0; i < jmin (s1, len); ++i)
            {
                float v = 0.0f;
                for (int ch = 0; ch < chans; ++ch)
                    v += buffer.getSample (ch, i);
                v /= (float) chans;
                lo = jmin (lo, v);
                hi = jmax (hi, v);
            }
            ov->mins[(size_t) bin] = lo;
            ov->maxs[(size_t) bin] = hi;
            ov->peak = jmax (ov->peak, hi, -lo);
        }
        return ov;
    }

    //==============================================================================
    void KickBank::run()
    {
        while (! threadShouldExit())
        {
            Request req;
            {
                const ScopedLock sl (requestLock);
                req = request;
            }

            if (req.generation == builtGeneration)
            {
                busy = false;
                wait (500);
                continue;
            }

            auto set = std::make_unique<KickSet>();
            DisplayInfo info;
            info.generation = req.generation;

            const auto global = decode (req.globalPath);
            info.global = global->info;
            info.globalOverview = makeOverview (global->audio.audio, global->audio.sampleRate);

            // split each distinct source once, for the bands that use it
            std::set<String> usedPaths { req.globalPath };
            std::array<String, kNumBands> sourceOf;
            for (int b = 0; b < kNumBands; ++b)
            {
                sourceOf[(size_t) b] = req.bandPaths[(size_t) b].isEmpty() ? req.globalPath : req.bandPaths[(size_t) b];
                info.overridden[(size_t) b] = req.bandPaths[(size_t) b].isNotEmpty();
                usedPaths.insert (sourceOf[(size_t) b]);
            }

            for (auto& path : usedPaths)
            {
                std::array<bool, 4> wanted {};
                bool any = false;
                for (int b = 0; b < kNumBands; ++b)
                    if (sourceOf[(size_t) b] == path)
                        wanted[(size_t) b] = any = true;
                if (! any)
                    continue;

                const auto src = decode (path);
                std::array<AudioBuffer<float>, 4> outs;
                dsp::splitIntoBands (src->audio.audio, src->audio.sampleRate, req.crossover, wanted, outs);

                for (int b = 0; b < kNumBands; ++b)
                {
                    if (! wanted[(size_t) b])
                        continue;
                    set->bands[(size_t) b].audio = std::move (outs[(size_t) b]);
                    set->bands[(size_t) b].sampleRate = src->audio.sampleRate;
                    info.bands[(size_t) b] = src->info;
                }

                if (threadShouldExit())
                    return;
            }

            for (int b = 0; b < kNumBands; ++b)
                info.bandOverviews[(size_t) b] = makeOverview (set->bands[(size_t) b].audio, set->bands[(size_t) b].sampleRate);

            const auto& sub = set->bands[0];
            if (sub.length() > 0)
            {
                // detect on a mono mix of the Sub band
                std::vector<float> mono ((size_t) sub.length());
                for (int i = 0; i < sub.length(); ++i)
                {
                    float v = 0.0f;
                    for (int ch = 0; ch < sub.audio.getNumChannels(); ++ch)
                        v += sub.audio.getSample (ch, i);
                    mono[(size_t) i] = v / (float) jmax (1, sub.audio.getNumChannels());
                }
                set->pitch = dsp::detectKickPitch (mono.data(), (int) mono.size(), sub.sampleRate);
            }
            info.pitch = set->pitch;

            // a newer request arrived while building: build that one instead
            {
                const ScopedLock sl (requestLock);
                if (request.generation != req.generation)
                    continue;
                request.preview = false;
            }

            set->preview = req.preview;
            set->id = nextSetId++;
            info.set = std::make_shared<KickSet> (*set);
            publish (std::move (set));
            builtGeneration = req.generation;

            // forget decoded files nothing uses any more
            for (auto it = cache.begin(); it != cache.end();)
                it = usedPaths.count (it->first) > 0 ? std::next (it) : cache.erase (it);

            info.loading = false;
            {
                const ScopedLock sl (displayLock);
                display = info;
            }
            sendChangeMessage();
        }
    }

    void KickBank::publish (std::unique_ptr<KickSet> set)
    {
        // if the audio thread never picked up the previous one, it was never seen: delete it
        delete pending.exchange (set.release(), std::memory_order_acq_rel);
    }

    const KickSet* KickBank::getAudioSet() noexcept
    {
        if (pending.load (std::memory_order_acquire) != nullptr && retiredFifo.getFreeSpace() > 0)
        {
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                {
                    const auto scope = retiredFifo.write (1);
                    if (scope.blockSize1 > 0)
                        retired[(size_t) scope.startIndex1] = current;
                }
                current = next;
            }
        }
        return current;
    }

    void KickBank::collectGarbage()
    {
        while (retiredFifo.getNumReady() > 0)
        {
            const auto scope = retiredFifo.read (1);
            if (scope.blockSize1 > 0)
            {
                delete retired[(size_t) scope.startIndex1];
                retired[(size_t) scope.startIndex1] = nullptr;
            }
        }
    }

    void KickBank::timerCallback()
    {
        collectGarbage();
    }
}
