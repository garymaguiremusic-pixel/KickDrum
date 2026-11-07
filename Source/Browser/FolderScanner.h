#pragma once

#include <juce_events/juce_events.h>
#include "SampleLibrary.h"

//==============================================================================
/**
    Finds audio files under one or more folders on a background thread and hands
    the list back on the message thread. A new scan cancels the previous one.
*/
class FolderScanner : private juce::Thread
{
public:
    static constexpr int kMaxFiles = 50000;

    FolderScanner();
    ~FolderScanner() override;

    /** 'wildcard' is e.g. "*.wav;*.aif;*.aiff;*.flac;*.mp3".
        Recursive: every audio file below the roots. Otherwise: the roots' own audio files and subfolders. */
    void scan (const juce::Array<juce::File>& roots, const juce::String& wildcard, bool recursive = true);
    void cancel();
    bool isScanning() const noexcept    { return isThreadRunning(); }

    /** Called on the message thread with the files found (sorted by path), the subfolders
        (non-recursive scans only, sorted by name), and whether the list was cut at kMaxFiles. */
    std::function<void (const juce::Array<juce::File>& files, const juce::Array<juce::File>& folders, bool truncated)> onFinished;

    /** Synchronous scan (used by the thread, and by tests). */
    static juce::Array<juce::File> findAudioFiles (const juce::Array<juce::File>& roots, const juce::String& wildcard,
                                                   std::function<bool()> shouldStop, bool& truncated);

    /** One folder's own audio files and (visible) subfolders, both in natural name order. */
    static void listFolder (const juce::File& dir, const juce::String& wildcard, std::function<bool()> shouldStop,
                            juce::Array<juce::File>& files, juce::Array<juce::File>& folders, bool& truncated);

private:
    void run() override;

    juce::CriticalSection lock;
    juce::Array<juce::File> pendingRoots;
    juce::String pendingWildcard;
    bool pendingRecursive = true;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

//==============================================================================
struct BrowserFilter
{
    juce::String query;            // words to find in the file name or tags (all must match)
    bool favouritesOnly = false;
    juce::String tag;              // empty = any tag
};

/** The files that pass the filter, in their original order. */
juce::Array<juce::File> applyBrowserFilter (const juce::Array<juce::File>&, const BrowserFilter&, const SampleLibrary&);
