#include "BrowserPanel.h"
#include "Shared/CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
Path makeStarPath (Rectangle<float> area)
{
    Path star;
    const auto c = area.getCentre();
    const float outer = jmin (area.getWidth(), area.getHeight()) * 0.5f;
    star.addStar (c.translated (0.0f, outer * 0.08f), 5, outer * 0.42f, outer, 0.0f);
    return star;
}

void StarButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    CustomLookAndFeel::drawInsetBox (g, b);
    const auto star = makeStarPath (b.reduced (b.getHeight() * 0.18f));
    const bool on = getToggleState();

    if (on)
    {
        g.setColour (VirusColours::blueGlow.withAlpha (0.4f));
        g.strokePath (star, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (VirusColours::blue);
        g.fillPath (star);
    }
    g.setColour ((on ? Colours::white : VirusColours::labelText).withAlpha (isHighlighted || isDown ? 1.0f : 0.8f));
    g.strokePath (star, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
namespace
{
    void styleList (ListBox& list)
    {
        list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
        list.setRowHeight (22);
        list.getVerticalScrollBar().setColour (ScrollBar::thumbColourId, VirusColours::blue.withAlpha (0.6f));
    }

    void styleEditor (TextEditor& ed, const String& hint)
    {
        ed.setFont (CustomLookAndFeel::uiFont (13.0f));
        ed.setColour (TextEditor::backgroundColourId, VirusColours::insetBottom);
        ed.setColour (TextEditor::textColourId, Colours::white);
        ed.setColour (TextEditor::outlineColourId, VirusColours::bevelDark);
        ed.setColour (TextEditor::focusedOutlineColourId, VirusColours::blue);
        ed.setColour (TextEditor::highlightColourId, VirusColours::blueGlow.withAlpha (0.6f));
        ed.setColour (CaretComponent::caretColourId, Colours::white);
        ed.setTextToShowWhenEmpty (hint, VirusColours::labelText.withAlpha (0.6f));
        ed.setIndents (6, 5);
    }

    void paintRowBackground (Graphics& g, int width, int height, bool selected, int row)
    {
        if (selected)
        {
            g.setColour (Colour (0xff2a6fb8));
            g.fillRect (0, 0, width, height);
        }
        else if (row % 2 == 1)
        {
            g.setColour (Colours::white.withAlpha (0.025f));
            g.fillRect (0, 0, width, height);
        }
    }
}

//==============================================================================
class BrowserPanel::PlacesModel : public ListBoxModel
{
public:
    explicit PlacesModel (BrowserPanel& p) : panel (p) {}

    int getNumRows() override   { return 2 + panel.library.getFolders().size(); }

    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override
    {
        // ListBox also paints filler rows past the end of the list
        if (! isPositiveAndBelow (row, getNumRows()))
            return;

        paintRowBackground (g, width, height, selected, row);
        auto r = Rectangle<int> (width, height).toFloat().reduced (6.0f, 0.0f);
        g.setFont (CustomLookAndFeel::uiFont (12.5f, row < 2));
        g.setColour (Colours::white);

        if (row == 0)
        {
            g.setColour (selected ? Colours::white : VirusColours::blue);
            g.fillPath (makeStarPath (r.removeFromLeft (16.0f).reduced (0.0f, 4.0f)));
            r.removeFromLeft (6.0f);
            g.setColour (Colours::white);
            g.drawText ("Favourites", r, Justification::centredLeft);
            return;
        }
        if (row == 1)
        {
            g.drawText ("All Folders", r, Justification::centredLeft);
            return;
        }

        const auto folder = panel.library.getFolders()[row - 2];
        g.setColour (folder.isDirectory() ? Colours::white : Colour (0xffffb0a8));
        g.drawText (folder.getFileName().isEmpty() ? folder.getFullPathName() : folder.getFileName(), r, Justification::centredLeft, true);
    }

    String getTooltipForRow (int row) override
    {
        if (! isPositiveAndBelow (row, getNumRows())) return {};
        if (row == 0) return "Every sample you have starred";
        if (row == 1) return "All of your sample folders";
        const auto folder = panel.library.getFolders()[row - 2];
        return folder.getFullPathName() + (folder.isDirectory() ? "" : "  (not found)");
    }

    void selectedRowsChanged (int row) override
    {
        if (row >= 0 && row != panel.state.place)
            panel.selectPlace (row);
    }

private:
    BrowserPanel& panel;
};

//==============================================================================
class BrowserPanel::TagsModel : public ListBoxModel
{
public:
    explicit TagsModel (BrowserPanel& p) : panel (p) {}

    void refresh()      { tags = panel.library.getAllTags(); }
    int indexOf (const String& tag) const
    {
        for (size_t i = 0; i < tags.size(); ++i)
            if (tags[i].first.equalsIgnoreCase (tag))
                return (int) i + 1;
        return 0;
    }

    int getNumRows() override   { return 1 + (int) tags.size(); }

    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override
    {
        // ListBox also paints filler rows past the end of the list
        if (! isPositiveAndBelow (row, getNumRows()))
            return;

        paintRowBackground (g, width, height, selected, row);
        auto r = Rectangle<int> (width, height).toFloat().reduced (6.0f, 0.0f);
        g.setFont (CustomLookAndFeel::uiFont (12.5f, row == 0));
        g.setColour (Colours::white);

        if (row == 0)
        {
            g.drawText ("Any Tag", r, Justification::centredLeft);
            return;
        }

        const auto& [name, count] = tags[(size_t) row - 1];
        g.drawText (name, r, Justification::centredLeft, true);
        g.setColour (selected ? Colours::white : VirusColours::labelText);
        g.drawText (String (count), r, Justification::centredRight);
    }

    void selectedRowsChanged (int row) override
    {
        panel.state.tag = isPositiveAndBelow (row - 1, (int) tags.size()) ? tags[(size_t) row - 1].first : String();
        panel.applyFilter();
    }

private:
    BrowserPanel& panel;
    std::vector<std::pair<String, int>> tags;
};

//==============================================================================
class BrowserPanel::FilesModel : public ListBoxModel
{
public:
    explicit FilesModel (BrowserPanel& p) : panel (p) {}

    static constexpr int starWidth = 26;
    bool suppressAutoLoad = false;

    int getNumRows() override   { return panel.numFolderRows() + panel.visible.size(); }

    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override
    {
        // ListBox also paints filler rows past the end of the list
        if (! isPositiveAndBelow (row, getNumRows()))
            return;

        paintRowBackground (g, width, height, selected, row);
        auto r = Rectangle<int> (width, height).toFloat();

        if (row < panel.numFolderRows())
        {
            const bool up = row < panel.numUpRows();
            const auto icon = r.removeFromLeft ((float) starWidth).reduced (5.0f, 5.0f);
            g.setColour (selected ? Colours::white : VirusColours::blue);
            if (up)
            {
                Path arrow;
                const auto c = icon.getCentre();
                arrow.startNewSubPath (c.x - 5.0f, c.y + 1.0f);
                arrow.lineTo (c.x, c.y - 4.0f);
                arrow.lineTo (c.x + 5.0f, c.y + 1.0f);
                arrow.startNewSubPath (c.x, c.y - 4.0f);
                arrow.lineTo (c.x, c.y + 5.0f);
                g.strokePath (arrow, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            else
            {
                g.fillPath (makeFolderPath (icon));
            }

            const String text = up ? ".." : panel.folderForRow (row).getFileName();
            g.setFont (CustomLookAndFeel::uiFont (12.5f, true));
            g.setColour (Colours::white);
            g.drawText (text, r.reduced (2.0f, 0.0f), Justification::centredLeft, true);

            g.setFont (CustomLookAndFeel::uiFont (11.0f));
            g.setColour (selected ? Colours::white.withAlpha (0.7f) : VirusColours::labelText.withAlpha (0.7f));
            g.drawText (up ? "Back up" : "Folder", r.withTrimmedRight (6.0f), Justification::centredRight, true);
            return;
        }

        const auto file = panel.visible[panel.fileIndexForRow (row)];
        const bool exists = file.existsAsFile();
        const bool current = panel.getCurrentSample && panel.getCurrentSample() == file;

        // star column
        const auto star = makeStarPath (r.removeFromLeft ((float) starWidth).reduced (6.0f, 4.0f));
        if (panel.library.isFavourite (file))
        {
            g.setColour (selected ? Colours::white : VirusColours::blue);
            g.fillPath (star);
        }
        else
        {
            g.setColour (Colours::white.withAlpha (0.25f));
            g.strokePath (star, PathStrokeType (1.0f));
        }

        r.removeFromRight (6.0f);
        const auto tags = panel.library.getTags (file);
        const auto folderArea = r.removeFromRight (jmin (220.0f, r.getWidth() * 0.3f));
        const auto tagsArea = r.removeFromRight (jmin (220.0f, r.getWidth() * 0.35f));

        g.setFont (CustomLookAndFeel::uiFont (12.5f, current));
        g.setColour (! exists ? Colour (0xffffb0a8) : (current && ! selected ? VirusColours::blue : Colours::white));
        g.drawText (file.getFileName() + (exists ? "" : "  (missing)"), r.reduced (2.0f, 0.0f), Justification::centredLeft, true);

        g.setFont (CustomLookAndFeel::uiFont (11.0f));
        g.setColour (selected ? Colours::white.withAlpha (0.85f) : Colour (0xff8fd0ff));
        g.drawText (tags.joinIntoString ("  "), tagsArea, Justification::centredLeft, true);

        // the containing folder only says something new in search results and favourites
        if (file.getParentDirectory() != panel.getCurrentFolder())
        {
            g.setColour (selected ? Colours::white.withAlpha (0.7f) : VirusColours::labelText.withAlpha (0.7f));
            g.drawText (file.getParentDirectory().getFileName(), folderArea, Justification::centredRight, true);
        }
    }

    String getTooltipForRow (int row) override
    {
        if (! isPositiveAndBelow (row, getNumRows()))
            return {};
        if (row < panel.numUpRows())
            return "Back up one level (Backspace)";
        if (row < panel.numFolderRows())
            return panel.folderForRow (row).getFullPathName();
        return panel.visible[panel.fileIndexForRow (row)].getFullPathName();
    }

    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        if (! isPositiveAndBelow (row, getNumRows()))
            return;

        // a click on a folder opens it
        if (row < panel.numFolderRows())
        {
            if (! e.mods.isPopupMenu())
                panel.activateRow (row, false);
            return;
        }

        if (e.mods.isPopupMenu())
        {
            panel.showRowMenu (row);
            return;
        }
        if (e.x < starWidth)
            panel.library.toggleFavourite (panel.visible[panel.fileIndexForRow (row)]);
    }

    // double-click / Enter: load a file and go back to the main page (or open a folder)
    void listBoxItemDoubleClicked (int row, const MouseEvent&) override   { panel.activateRow (row, true); }
    void returnKeyPressed (int row) override                             { panel.activateRow (row, true); }
    void deleteKeyPressed (int) override                                 { panel.goUp(); }   // Backspace

    void selectedRowsChanged (int row) override
    {
        panel.updateTagEditor();
        if (! suppressAutoLoad && panel.state.autoLoad && panel.fileIndexForRow (row) >= 0)
            panel.loadRow (row);
    }

private:
    static Path makeFolderPath (Rectangle<float> area)
    {
        auto b = area.withSizeKeepingCentre (area.getWidth(), area.getWidth() * 0.78f);
        Path p;
        const float tabW = b.getWidth() * 0.42f, tabH = b.getHeight() * 0.2f;
        p.addRoundedRectangle (b.getX(), b.getY(), tabW, tabH * 2.0f, 1.5f);
        p.addRoundedRectangle (b.getX(), b.getY() + tabH, b.getWidth(), b.getHeight() - tabH, 1.5f);
        return p;
    }

    BrowserPanel& panel;
};

//==============================================================================
BrowserPanel::BrowserPanel (SampleLibrary& lib, State& s, String fileWildcard, RangedAudioParameter* keyLockParam)
    : BrowserPanel (lib, s, std::move (fileWildcard), keyLockParam, Options {})
{
}

BrowserPanel::BrowserPanel (SampleLibrary& lib, State& s, String fileWildcard, RangedAudioParameter* keyLockParam, Options opts)
    : library (lib), state (s), wildcard (std::move (fileWildcard)), options (std::move (opts))
{
    placesModel = std::make_unique<PlacesModel> (*this);
    tagsModel = std::make_unique<TagsModel> (*this);
    filesModel = std::make_unique<FilesModel> (*this);
    placesList.setModel (placesModel.get());
    tagsList.setModel (tagsModel.get());
    filesList.setModel (filesModel.get());

    for (auto* list : { &placesList, &tagsList, &filesList })
    {
        styleList (*list);
        addAndMakeVisible (list);
    }
    filesList.setRowHeight (24);
    filesList.setMultipleSelectionEnabled (false);

    addFolderButton.setTooltip ("Add a folder to Places. Click it to browse its subfolders and " + options.nounPlural + ".");
    addFolderButton.onClick = [this] { addFolder(); };
    removeFolderButton.setTooltip ("Remove the selected folder from the list (files are not touched)");
    removeFolderButton.onClick = [this] { removeSelectedFolder(); };

    styleEditor (searchBox, "Search names and tags");
    searchBox.setText (state.query, false);
    searchBox.onTextChange = [this] { state.query = searchBox.getText(); applyFilter(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); };

    styleEditor (tagEditor, "Tags for the selected " + options.noun + ", e.g.  kick, dark, 808");
    tagEditor.onReturnKey = [this] { applyTagEditor(); };
    applyTagsButton.setTooltip ("Save these tags on the selected " + options.noun + " (comma-separated)");
    applyTagsButton.onClick = [this] { applyTagEditor(); };

    favOnlyButton.setClickingTogglesState (true);
    favOnlyButton.setToggleState (state.favouritesOnly, dontSendNotification);
    favOnlyButton.setTooltip ("Show only starred " + options.nounPlural);
    favOnlyButton.onClick = [this] { state.favouritesOnly = favOnlyButton.getToggleState(); applyFilter(); };

    autoLoadButton.setClickingTogglesState (true);
    autoLoadButton.setToggleState (state.autoLoad, dontSendNotification);
    autoLoadButton.setTooltip ("Load a " + options.noun + " as soon as it is selected (arrow keys audition quickly)");
    autoLoadButton.onClick = [this] { state.autoLoad = autoLoadButton.getToggleState(); };

    closeButton.onClick = [this] { if (onClose) onClose(); };

    previewButton.setClickingTogglesState (true);
    previewButton.setToggleState (state.preview, dontSendNotification);
    previewButton.setTooltip ("Audition: play the kick once each time you browse to a sample");
    previewButton.onClick = [this] { state.preview = previewButton.getToggleState(); };

    keyLockButton.setClickingTogglesState (true);
    keyLockButton.setTooltip ("Key Lock: detect each sample's pitch and set Root/Fine so it plays in the Scale Lock key");
    if (keyLockParam != nullptr)
        keyLockAttachment = std::make_unique<ButtonParameterAttachment> (*keyLockParam, keyLockButton);

    for (auto* c : std::initializer_list<Component*> { &addFolderButton, &removeFolderButton, &searchBox, &tagEditor,
                                                       &applyTagsButton, &favOnlyButton, &autoLoadButton, &closeButton,
                                                       &previewButton })
        addAndMakeVisible (c);
    addChildComponent (keyLockButton);
    keyLockButton.setVisible (keyLockParam != nullptr);
    previewButton.setVisible (options.showPreview);

    scanner.onFinished = [this] (const Array<File>& files, const Array<File>& subfolders, bool wasTruncated)
    {
        scanned = files;
        folders = subfolders;
        truncated = wasTruncated;
        scanning = false;
        applyFilter();
    };

    library.addChangeListener (this);
    tagsModel->refresh();
    tagsList.updateContent();
    tagsList.selectRow (tagsModel->indexOf (state.tag), true, true);
    refreshPlaces();

    // reopen the folder that was being viewed, if it still belongs to the place
    const auto folderToRestore = state.folder;
    selectPlace (jlimit (0, 1 + library.getFolders().size(), state.place));
    if (folderToRestore.isNotEmpty())
        openFolder (File (folderToRestore));
}

BrowserPanel::~BrowserPanel()
{
    keyLockAttachment.reset();
    library.removeChangeListener (this);
    scanner.onFinished = nullptr;
    scanner.cancel();
    placesList.setModel (nullptr);
    tagsList.setModel (nullptr);
    filesList.setModel (nullptr);
}

void BrowserPanel::visibilityChanged()
{
    if (isVisible())
    {
        library.reloadIfChanged();
        startTimer (2000);
    }
    else
    {
        stopTimer();
    }
}

void BrowserPanel::timerCallback()
{
    library.reloadIfChanged();

    const auto current = getCurrentSample ? getCurrentSample() : File();
    if (current != lastCurrent)
    {
        lastCurrent = current;
        filesList.repaint();
    }
}

void BrowserPanel::changeListenerCallback (ChangeBroadcaster*)
{
    // favourites, tags or folders changed (here or in another instance)
    tagsModel->refresh();
    tagsList.updateContent();
    tagsList.selectRow (tagsModel->indexOf (state.tag), true, true);
    refreshPlaces();

    if (state.place == 0)
        scanned = library.getFavourites();
    else if (getCurrentFolder() == File() && ! scannedRecursive)
        folders = library.getFolders();
    applyFilter();
    updateTagEditor();
}

//==============================================================================
void BrowserPanel::refreshPlaces()
{
    placesList.updateContent();
    placesList.selectRow (state.place, true, true);
    placesList.repaint();
    removeFolderButton.setEnabled (state.place >= 2);
}

void BrowserPanel::selectPlace (int index)
{
    const int numPlaces = 2 + library.getFolders().size();
    state.place = jlimit (0, numPlaces - 1, index);
    state.folder.clear();
    selectAfterScan = File();
    placesList.selectRow (state.place, true, true);
    removeFolderButton.setEnabled (state.place >= 2);
    rescan();
}

File BrowserPanel::getCurrentFolder() const
{
    if (state.place == 0)
        return {};
    if (state.folder.isNotEmpty())
        return File (state.folder);
    return state.place >= 2 ? library.getFolders()[state.place - 2] : File();
}

bool BrowserPanel::canGoUp() const
{
    return state.place >= 1 && state.folder.isNotEmpty();
}

void BrowserPanel::openFolder (const File& dir)
{
    if (state.place == 0 || ! dir.isDirectory())
        return;

    // only folders inside the place can be opened
    const auto roots = state.place == 1 ? library.getFolders() : Array<File> { library.getFolders()[state.place - 2] };
    bool inside = false;
    for (auto& root : roots)
        inside = inside || dir == root || dir.isAChildOf (root);
    if (! inside)
        return;

    const bool isPlaceRoot = state.place >= 2 && dir == roots.getFirst();
    state.folder = isPlaceRoot ? String() : dir.getFullPathName();
    selectAfterScan = File();
    rescan();
    filesList.scrollToEnsureRowIsOnscreen (0);
}

bool BrowserPanel::goUp()
{
    if (! canGoUp())
        return false;

    const auto current = File (state.folder);
    const auto parent = current.getParentDirectory();

    if (state.place == 1 && library.getFolders().contains (current))
        state.folder.clear();                                       // back to the list of folders
    else if (state.place >= 2 && parent == library.getFolders()[state.place - 2])
        state.folder.clear();
    else
        state.folder = parent.getFullPathName();

    selectAfterScan = current;
    rescan();
    return true;
}

bool BrowserPanel::isSearching() const
{
    return state.query.trim().isNotEmpty() || state.favouritesOnly || state.tag.isNotEmpty();
}

void BrowserPanel::activateRow (int row, bool closeAfter)
{
    if (row < 0)
        return;
    if (row < numUpRows())
        goUp();
    else if (row < numFolderRows())
        openFolder (folderForRow (row));
    else
        loadRow (row, closeAfter);
}

String BrowserPanel::locationText() const
{
    if (state.place == 0)
        return "Favourites";

    const auto dir = getCurrentFolder();
    if (dir == File())
        return "All Folders";

    // path from the top folder of the place, e.g.  Drums  >  Acoustic  >  Snares
    File top;
    for (auto& root : library.getFolders())
        if (dir == root || dir.isAChildOf (root))
            if (top == File() || root.isAChildOf (top))
                top = root;

    StringArray parts;
    for (auto f = dir; f != top && f != File() && f.isAChildOf (top); f = f.getParentDirectory())
        parts.insert (0, f.getFileName());
    parts.insert (0, top.getFileName().isEmpty() ? top.getFullPathName() : top.getFileName());
    return parts.joinIntoString ("  >  ") + (isSearching() ? "  (and subfolders)" : "");
}

void BrowserPanel::rescan()
{
    truncated = false;

    if (state.place == 0)
    {
        scanner.cancel();
        scanning = false;
        scannedRecursive = false;
        folders.clear();
        scanned = library.getFavourites();
        applyFilter();
        return;
    }

    scannedRecursive = isSearching();
    const auto dir = getCurrentFolder();
    Array<File> roots;
    if (dir != File())
        roots.add (dir);
    else
        roots = library.getFolders();       // top of All Folders

    scanned.clear();
    folders.clear();

    // the top of All Folders just lists the folders, unless a search covers all of them
    if (dir == File() && ! scannedRecursive)
    {
        scanner.cancel();
        scanning = false;
        folders = roots;
        applyFilter();
        return;
    }

    scanning = ! roots.isEmpty();
    applyFilter();
    if (scanning)
        scanner.scan (roots, wildcard, scannedRecursive);
}

void BrowserPanel::applyFilter()
{
    // searching lists every match below the folder; browsing shows one folder at a time
    if (state.place != 0 && isSearching() != scannedRecursive)
    {
        rescan();
        return;
    }

    const auto previous = selectedFile();
    visible = applyBrowserFilter (scanned, { state.query, state.favouritesOnly, state.tag }, library);
    if (scannedRecursive)
        folders.clear();

    filesModel->suppressAutoLoad = true;    // keeping the selection must not reload the sample
    filesList.updateContent();
    int row = rowForFile (visible.indexOf (previous));
    if (row < 0 && selectAfterScan != File() && folders.contains (selectAfterScan) && ! scanning)
    {
        row = numUpRows() + folders.indexOf (selectAfterScan);
        selectAfterScan = File();
    }
    if (row >= 0)
    {
        filesList.selectRow (row, true, true);
        filesList.scrollToEnsureRowIsOnscreen (row);
    }
    else
    {
        filesList.deselectAllRows();
    }
    filesModel->suppressAutoLoad = false;

    filesList.repaint();
    updateTagEditor();
    repaint();
}

File BrowserPanel::selectedFile() const
{
    return visible[fileIndexForRow (filesList.getSelectedRow())];
}

void BrowserPanel::loadRow (int row, bool closeAfter)
{
    const auto file = visible[fileIndexForRow (row)];
    if (file.existsAsFile() && onLoad)
        onLoad (file);
    filesList.repaint();

    if (closeAfter && onClose)
        onClose();
}

bool BrowserPanel::step (int direction)
{
    if (visible.isEmpty())
        return false;

    // move from the loaded sample if it's in the list, else from the selection
    const auto current = getCurrentSample ? getCurrentSample() : File();
    int index = visible.indexOf (current);
    if (index < 0)
        index = fileIndexForRow (filesList.getSelectedRow());

    const int n = visible.size();
    index = index < 0 ? (direction > 0 ? 0 : n - 1)
                      : ((index + (direction > 0 ? 1 : -1)) % n + n) % n;

    // skip files that have gone missing
    for (int tries = 0; tries < n && ! visible[index].existsAsFile(); ++tries)
        index = ((index + (direction > 0 ? 1 : -1)) % n + n) % n;

    filesModel->suppressAutoLoad = true;
    filesList.selectRow (rowForFile (index));
    filesModel->suppressAutoLoad = false;
    loadRow (rowForFile (index));
    return true;
}

void BrowserPanel::updateTagEditor()
{
    const auto file = selectedFile();
    const bool has = file != File();
    tagEditor.setEnabled (has);
    applyTagsButton.setEnabled (has);
    if (! tagEditor.hasKeyboardFocus (true))
        tagEditor.setText (has ? library.getTags (file).joinIntoString (", ") : String(), false);
}

void BrowserPanel::applyTagEditor()
{
    const auto file = selectedFile();
    if (file != File())
        library.setTags (file, SampleLibrary::parseTags (tagEditor.getText()));
    filesList.grabKeyboardFocus();
}

void BrowserPanel::showRowMenu (int row)
{
    const auto file = visible[fileIndexForRow (row)];
    if (file == File())
        return;

    filesModel->suppressAutoLoad = true;
    filesList.selectRow (row);
    filesModel->suppressAutoLoad = false;

    const bool fav = library.isFavourite (file);
    const auto tags = library.getTags (file);

    PopupMenu addTags, removeTags;
    for (auto& [name, count] : library.getAllTags())
        if (! tags.contains (name, true))
            addTags.addItem (name, [this, file, name = name] { library.addTag (file, name); });
    for (auto& t : tags)
        removeTags.addItem (t, [this, file, t] { library.removeTag (file, t); });

    PopupMenu menu;
    menu.addItem ("Load", file.existsAsFile(), false, [this, row] { loadRow (row); });
    menu.addItem (fav ? "Remove from Favourites" : "Add to Favourites", [this, file] { library.toggleFavourite (file); });
    menu.addSeparator();
    menu.addSubMenu ("Add Tag", addTags, addTags.getNumItems() > 0);
    menu.addSubMenu ("Remove Tag", removeTags, removeTags.getNumItems() > 0);
    menu.addItem ("Edit Tags...", [this] { tagEditor.grabKeyboardFocus(); tagEditor.selectAll(); });
    menu.addSeparator();
   #if JUCE_MAC
    menu.addItem ("Show in Finder", file.exists(), false, [file] { file.revealToUser(); });
   #else
    menu.addItem ("Show in Explorer", file.exists(), false, [file] { file.revealToUser(); });
   #endif
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&filesList).withMousePosition());
}

void BrowserPanel::addFolder()
{
    chooser = std::make_unique<FileChooser> ("Add a sample folder", File::getSpecialLocation (File::userMusicDirectory));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this] (const FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              library.addFolder (dir);
                              selectPlace (2 + library.getFolders().indexOf (dir));
                          });
}

void BrowserPanel::removeSelectedFolder()
{
    if (state.place < 2)
        return;
    library.removeFolder (library.getFolders()[state.place - 2]);
    selectPlace (1);
}

//==============================================================================
void BrowserPanel::paint (Graphics& g)
{
    // swallow clicks on the panel and hide what's underneath
    g.setColour (VirusColours::background.withAlpha (0.97f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    CustomLookAndFeel::drawPanel (g, panelBounds, options.title);

    auto label = [&g] (const char* text, Rectangle<int> area)
    {
        CustomLookAndFeel::drawSmallLabel (g, text, area.toFloat(), Justification::centredLeft);
    };
    label ("Places", { 12, 24, 200, 14 });
    label ("Tags", { 12, 290, 200, 14 });
    label ("Tags for selected", { 240, getHeight() - 32, 130, 14 });

    for (auto* list : { &placesList, &tagsList, &filesList })
        CustomLookAndFeel::drawInsetBox (g, list->getBounds().toFloat().expanded (2.0f));

    // where we are
    g.setFont (CustomLookAndFeel::uiFont (12.5f, true));
    g.setColour (VirusColours::blue);
    g.drawText (locationText(), locationBounds, Justification::centredLeft, true);

    // status
    String status;
    if (scanning)
        status = "Scanning...";
    else if (state.place != 0 && library.getFolders().isEmpty())
        status = "Add a folder with + Folder";
    else
        status = (folders.isEmpty() ? String() : String (folders.size()) + (folders.size() == 1 ? " folder, " : " folders, "))
               + String (visible.size()) + (visible.size() == 1 ? " " + options.noun : " " + options.nounPlural)
               + (visible.size() != scanned.size() ? " (of " + String (scanned.size()) + ")" : String())
               + (truncated ? "  - first " + String (FolderScanner::kMaxFiles) + " only" : String());

    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    g.setColour (VirusColours::labelText);
    g.drawText (status, Rectangle<int> (788, 28, getWidth() - 10 - 70 - 6 - 788, 22), Justification::centredRight);

    if (visible.isEmpty() && folders.isEmpty() && ! canGoUp() && ! scanning)
    {
        g.setColour (Colours::white.withAlpha (0.45f));
        g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
        const String hint = state.place == 0 && scanned.isEmpty()
                              ? "No favourites yet: click the star next to a " + options.noun + " to add it"
                              : (scanned.isEmpty() ? (state.place == 1 && library.getFolders().isEmpty() ? "" : "No " + options.nounPlural + " here")
                                                   : "Nothing matches the filter");
        g.drawText (hint, filesList.getBounds(), Justification::centred);
    }
}

void BrowserPanel::resized()
{
    panelBounds = getLocalBounds().toFloat();

    placesList.setBounds (12, 42, 216, 208);
    addFolderButton.setBounds (10, 256, 107, 24);
    removeFolderButton.setBounds (121, 256, 107, 24);
    tagsList.setBounds (12, 308, 216, getHeight() - 318);

    const int right = getWidth() - 10;
    searchBox.setBounds (240, 26, 200, 26);
    favOnlyButton.setBounds (446, 27, 104, 24);
    autoLoadButton.setBounds (554, 27, 78, 24);
    previewButton.setBounds (636, 27, 70, 24);
    keyLockButton.setBounds (710, 27, 74, 24);
    closeButton.setBounds (right - 70, 27, 70, 24);

    locationBounds = { 244, 58, right - 246, 20 };
    filesList.setBounds (242, 82, right - 244, getHeight() - 82 - 46);
    tagEditor.setBounds (372, getHeight() - 38, right - 372 - 96, 26);
    applyTagsButton.setBounds (right - 88, getHeight() - 37, 88, 24);
}
