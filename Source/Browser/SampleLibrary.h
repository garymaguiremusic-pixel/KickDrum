#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <map>

//==============================================================================
/**
    The user's sample library: favourite samples, tags and the browser's folders.

    It is stored once per computer (not per project), so every plugin instance and
    every session sees the same favourites:
        Windows  %APPDATA%\KickDrum\SampleLibrary.xml
        macOS    ~/Library/Application Support/KickDrum/SampleLibrary.xml
        Linux    ~/.config/KickDrum/SampleLibrary.xml

    Every change re-reads the file under an inter-process lock, applies the change
    and writes it back, so two DAWs open at once don't overwrite each other.
    Message thread only (never touched by the audio thread).
*/
class SampleLibrary : public juce::ChangeBroadcaster
{
public:
    struct SampleInfo
    {
        bool favourite = false;
        juce::StringArray tags;
        bool isEmpty() const    { return ! favourite && tags.isEmpty(); }
    };

    /** Uses the default per-user location. */
    SampleLibrary();
    /** Uses a specific file (tests). */
    explicit SampleLibrary (const juce::File& storageFile);

    static juce::File getDefaultStorageFile();
    const juce::File& getStorageFile() const noexcept     { return file; }

    // Folders shown under "Places" ------------------------------------------------
    juce::Array<juce::File> getFolders() const;
    void addFolder (const juce::File&);
    void removeFolder (const juce::File&);

    // Favourites and tags (keyed by absolute path) -----------------------------------
    bool isFavourite (const juce::File&) const;
    void setFavourite (const juce::File&, bool);
    void toggleFavourite (const juce::File& f)      { setFavourite (f, ! isFavourite (f)); }

    juce::StringArray getTags (const juce::File&) const;
    void setTags (const juce::File&, const juce::StringArray&);
    void addTag (const juce::File&, const juce::String&);
    void removeTag (const juce::File&, const juce::String&);

    juce::Array<juce::File> getFavourites() const;
    /** Every tag in use, sorted case-insensitively, with how many samples carry it. */
    std::vector<std::pair<juce::String, int>> getAllTags() const;

    /** Splits "kick, Dark ,808" into tidy unique tags (case-insensitive duplicates removed). */
    static juce::StringArray parseTags (const juce::String&);

    /** Re-reads the file if another instance changed it. Returns true if anything changed. */
    bool reloadIfChanged();

private:
    struct Data
    {
        juce::Array<juce::File> folders;
        std::map<juce::String, SampleInfo> samples;
    };

    void load();
    void save();
    juce::int64 fileHash() const;
    void modify (std::function<void (Data&)>);
    static juce::String key (const juce::File& f)   { return f.getFullPathName(); }

    juce::File file;
    Data data;
    juce::int64 loadedHash = 0;     // hash of the file contents we last read or wrote
};
