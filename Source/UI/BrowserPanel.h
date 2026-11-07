#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Browser/SampleLibrary.h"
#include "../Browser/FolderScanner.h"

//==============================================================================
/** A five-pointed star outline, filled when on. Used for favourites. */
juce::Path makeStarPath (juce::Rectangle<float> area);

/** Small toggle drawn as a star (favourite the loaded sample). */
class StarButton : public juce::Button
{
public:
    StarButton() : juce::Button ("favourite") { setClickingTogglesState (false); }
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

//==============================================================================
/**
    Sample browser: favourite folders on the left ("Places") with a tag filter,
    the files on the right with a star column, search, "favourites only", and a
    tag editor for the selected file. Favourites/tags live in SampleLibrary.
*/
class BrowserPanel : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    /** UI state kept by the processor, so reopening the editor restores it. */
    struct State
    {
        int place = 0;               // 0 = Favourites, 1 = All folders, 2+ = a folder
        bool autoLoad = true;
        bool preview = true;          // play each browsed sample once on the key's root
        bool favouritesOnly = false;
        juce::String tag, query;
        juce::String folder;         // folder being viewed inside the place (empty = the place itself)
    };

    /** What the panel is for (the sample browser by default). */
    struct Options
    {
        juce::String title { "Sample Browser" };
        juce::String noun { "sample" }, nounPlural { "samples" };
        bool showPreview = true;
    };

    /** 'keyLockParam' (optional) is shown as a Key Lock toggle. */
    BrowserPanel (SampleLibrary&, State&, juce::String fileWildcard, juce::RangedAudioParameter* keyLockParam = nullptr);
    BrowserPanel (SampleLibrary&, State&, juce::String fileWildcard, juce::RangedAudioParameter* keyLockParam, Options);
    ~BrowserPanel() override;

    std::function<void (const juce::File&)> onLoad;       // load a sample into the plugin
    std::function<juce::File()> getCurrentSample;         // highlight the loaded sample
    std::function<void()> onClose;

    /** Loads the previous (-1) or next (+1) sample in the current list (wraps around).
        Works while the browser is hidden. Returns false if the list is empty. */
    bool step (int direction);
    bool hasFiles() const noexcept        { return ! visible.isEmpty(); }

    void refreshPlaces();       // folders or favourites changed
    void visibilityChanged() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shows a folder's subfolders and audio files in the main list. */
    void openFolder (const juce::File&);
    /** Back to the parent folder (or the place's top level). Returns false when already there. */
    bool goUp();

    // exposed for tests
    const juce::Array<juce::File>& getVisibleFiles() const noexcept   { return visible; }
    const juce::Array<juce::File>& getVisibleFolders() const noexcept { return folders; }
    juce::File getCurrentFolder() const;     // empty at the top of All Folders and in Favourites
    bool canGoUp() const;
    void selectPlace (int index);

private:
    class PlacesModel;
    class TagsModel;
    class FilesModel;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rescan();
    void applyFilter();
    void loadRow (int row, bool closeAfter = false);
    void showRowMenu (int row);
    void updateTagEditor();
    void applyTagEditor();
    void addFolder();
    void removeSelectedFolder();
    juce::File selectedFile() const;

    bool isSearching() const;                 // search/tag/favourites filter: list every match below the folder
    int numUpRows() const                     { return canGoUp() ? 1 : 0; }
    int numFolderRows() const                 { return numUpRows() + folders.size(); }
    int rowForFile (int fileIndex) const      { return fileIndex < 0 ? -1 : fileIndex + numFolderRows(); }
    int fileIndexForRow (int row) const       { return row < numFolderRows() ? -1 : row - numFolderRows(); }
    juce::File folderForRow (int row) const   { return folders[row - numUpRows()]; }
    void activateRow (int row, bool closeAfter);   // open a folder / go up / load a file
    juce::String locationText() const;

    SampleLibrary& library;
    State& state;
    juce::String wildcard;
    Options options;
    FolderScanner scanner;

    juce::Array<juce::File> scanned, visible, folders;
    bool scanning = false, truncated = false, scannedRecursive = false;
    juce::File selectAfterScan;              // re-select the folder we came up from

    std::unique_ptr<PlacesModel> placesModel;
    std::unique_ptr<TagsModel> tagsModel;
    std::unique_ptr<FilesModel> filesModel;
    juce::ListBox placesList, tagsList, filesList;

    juce::TextButton addFolderButton { "+ Folder" }, removeFolderButton { "Remove" };
    juce::TextEditor searchBox, tagEditor;
    juce::TextButton favOnlyButton { "Favourites Only" }, autoLoadButton { "Auto Load" };
    juce::TextButton applyTagsButton { "Set Tags" }, closeButton { "Close" };
    juce::TextButton previewButton { "Preview" }, keyLockButton { "Key Lock" };
    std::unique_ptr<juce::ButtonParameterAttachment> keyLockAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<float> panelBounds;
    juce::Rectangle<int> locationBounds;
    juce::File lastCurrent;
};
