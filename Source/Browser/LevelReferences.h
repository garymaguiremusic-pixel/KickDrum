#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <array>
#include <vector>

//==============================================================================
/**
    Named band-level references ("Techno", "Trap 808" ...): the four bands' levels of a
    kick you're happy with, to match new kicks against. Stored once per computer,
    next to the sample library:
        Windows  %APPDATA%\KickDrum\LevelReferences.xml
    so every project and plugin instance shares them. Message thread only.
*/
class LevelReferences : public juce::ChangeBroadcaster
{
public:
    static constexpr int kNumBands = 4;

    struct Reference
    {
        juce::String name;
        std::array<float, kNumBands> rmsDb { -100.0f, -100.0f, -100.0f, -100.0f };
        std::array<float, kNumBands> peakDb { -100.0f, -100.0f, -100.0f, -100.0f };
        std::array<bool, kNumBands> active { true, true, true, true };
    };

    LevelReferences();
    explicit LevelReferences (const juce::File& storageFile);    // tests

    static juce::File getDefaultStorageFile();

    std::vector<Reference> getAll() const;           // sorted by name
    bool find (const juce::String& name, Reference& out) const;
    /** Adds or replaces (by name, ignoring case). */
    void save (const Reference&);
    void remove (const juce::String& name);
    /** Re-reads the file if another instance changed it. */
    void reloadIfChanged();

private:
    void load();
    void write();

    juce::File file;
    std::vector<Reference> refs;
    juce::int64 loadedHash = 0;
};
