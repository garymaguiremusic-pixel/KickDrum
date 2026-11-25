#include "PluginEditor.h"

using namespace juce;
using namespace kd;

//==============================================================================
KickDrumAudioProcessorEditor::KickDrumAudioProcessorEditor (KickDrumAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), content (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    // Resizable with a fixed aspect ratio, 70 % to 200 % of the base size.
    setResizable (true, true);
    setResizeLimits (kBaseWidth * 7 / 10, kBaseHeight * 7 / 10, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);

    const auto saved = processor.getEditorSize();
    if (saved.x >= kBaseWidth * 7 / 10 && saved.x <= kBaseWidth * 2)
        setSize (saved.x, roundToInt (saved.x * (double) kBaseHeight / kBaseWidth));
    else
        setSize (kBaseWidth, kBaseHeight);

    startTimerHz (30);
}

KickDrumAudioProcessorEditor::~KickDrumAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KickDrumAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseWidth;
    content.setBounds (0, 0, kBaseWidth, kBaseHeight);
    content.setTransform (AffineTransform::scale (scale));
    processor.setEditorSize ({ getWidth(), getHeight() });
}

void KickDrumAudioProcessorEditor::timerCallback()
{
    content.updateDisplays();
}

//==============================================================================
KickDrumAudioProcessorEditor::BandControls::BandControls (KickDrumAudioProcessor& p, int b)
    : band (b),
      view (b),
      levelStrip (b),
      attackModeButtons (*p.apvts.getParameter (bandParam (b, BandParams::attackMode)), { "ms", "Sync" },
                         "Attack in milliseconds, or Sync: the attack lasts a note length (1/64 to 1/4) at the tempo"),
      modeButtons (*p.apvts.getParameter (bandParam (b, BandParams::decayMode)), { "ms", "Sync" },
                   "Length in milliseconds, or Sync: the band lasts exactly a note length at the tempo")
{
}

std::vector<Component*> KickDrumAudioProcessorEditor::BandControls::components()
{
    std::vector<Component*> list { &view, &levelStrip, &sampleLcd, &star, &back, &next, &browse, &useMain,
                                   &attackModeButtons, &modeButtons, &attackSyncBox, &syncBox, &mute, &solo, &phase };
    for (auto* k : knobs())
        list.push_back (k);
    for (auto& l : valueLcds)
        list.push_back (&l);
    return list;
}

//==============================================================================
namespace
{
    BrowserPanel::Options browserOptions (const String& title)
    {
        BrowserPanel::Options o;
        o.title = title;
        o.noun = "kick";
        o.nounPlural = "kicks";
        o.showPreview = true;
        return o;
    }

    String rangeText (int band, const kd::dsp::CrossoverSettings& s)
    {
        switch (band)
        {
            case 0:  return "20 - " + formatHz (s.freqs[0]);
            case 1:  return formatHz (s.freqs[0]) + " - " + formatHz (s.freqs[1]);
            case 2:  return formatHz (s.freqs[1]) + " - " + formatHz (s.freqs[2]);
            default: return formatHz (s.freqs[2]) + " +";
        }
    }
}

KickDrumAudioProcessorEditor::Content::Content (KickDrumAudioProcessor& p)
    : processor (p),
      bank (p.getBank()),
      mainWave (*p.apvts.getParameter (ParamIDs::sampleStart), *p.apvts.getParameter (ParamIDs::sampleEnd)),
      crossover (*p.apvts.getParameter (ParamIDs::xover1), *p.apvts.getParameter (ParamIDs::xover2),
                 *p.apvts.getParameter (ParamIDs::xover3), *p.apvts.getParameter (ParamIDs::slope)),
      slopeButtons (*p.apvts.getParameter (ParamIDs::slope), { "6", "12", "18", "24", "36" },
                    "Crossover slope in dB/oct. Gentler slopes ring less; steeper ones keep the bands more separate"),
      tailStartModeButtons (*p.apvts.getParameter (ParamIDs::tailStartMode), { "ms", "Sync" },
                            "Tail Start in milliseconds, or Sync: the tail takes over after a note length (1/64 to 1/4)"),
      tempoField (*p.apvts.getParameter (ParamIDs::bpm)),
      globalBrowser (p.getSampleLibrary(), p.getBrowserState (KickDrumAudioProcessor::kGlobalBrowser),
                     p.getBank().getSupportedWildcard(), nullptr, browserOptions ("Kick Browser  -  loads into every band"))
{
    auto isSupported = [this] (const File& f) { return bank.isSupportedFile (f); };

    // Top bar ----------------------------------------------------------------------------
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (String (i + 1).paddedLeft ('0', 2) + "  " + processor.getProgramName (i), i + 1);
    presetBox.setSelectedId (processor.getCurrentProgram() + 1, dontSendNotification);
    presetBox.setTooltip ("Factory presets (they keep the loaded samples)");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
    };
    addAndMakeVisible (presetBox);

    favouriteButton.setTooltip ("Add the main kick to your favourites (or remove it)");
    favouriteButton.onClick = [this]
    {
        const auto f = bank.getGlobalFile();
        if (f.existsAsFile())
            processor.getSampleLibrary().toggleFavourite (f);
        updateDisplays();
    };
    addAndMakeVisible (favouriteButton);

    backButton.setTooltip ("Previous kick in the browser (the folder you are viewing, or the search results), into every band");
    nextButton.setTooltip ("Next kick in the browser (the folder you are viewing, or the search results), into every band");
    backButton.onClick = [this] { globalBrowser.step (-1); };
    nextButton.onClick = [this] { globalBrowser.step (+1); };
    addAndMakeVisible (backButton);
    addAndMakeVisible (nextButton);

    browserButton.setClickingTogglesState (true);
    browserButton.setTooltip ("Browse your kick folders, favourites and tags. The kick you pick loads into every band");
    browserButton.onClick = [this] { showBrowser (browserButton.getToggleState() ? KickDrumAudioProcessor::kGlobalBrowser : -1); };
    addAndMakeVisible (browserButton);

    loadButton.setTooltip ("Load a WAV, AIFF, FLAC or MP3 kick into every band (or drop a file on the waveform)");
    loadButton.onClick = [this] { browseForFile(); };
    addAndMakeVisible (loadButton);

    playButton.setTooltip ("Play the kick (MIDI notes play it too)");
    playButton.onClick = [this] { processor.audition(); };
    addAndMakeVisible (playButton);

    // Kick + crossover -------------------------------------------------------------------
    mainWave.isSupported = isSupported;
    mainWave.onFileDropped = [this] (const File& f) { processor.loadGlobalFromUser (f); };
    mainWave.onAudition = [this] { processor.audition(); };
    mainWave.onBrowse = [this] { browseForFile(); };
    addAndMakeVisible (mainWave);
    addAndMakeVisible (crossover);
    addAndMakeVisible (slopeButtons);
    for (int i = 0; i < 3; ++i)
    {
        xoverFields[(size_t) i] = std::make_unique<kd::ui::FrequencyField> (*p.apvts.getParameter ("xover" + String (i + 1)));
        addAndMakeVisible (*xoverFields[(size_t) i]);
    }

    // Output display
    outputView.onAudition = [this] { processor.audition(); };
    addAndMakeVisible (outputView);
    addAndMakeVisible (outLengthLcd);
    addAndMakeVisible (outPeakLcd);
    dragAudio.createFile = [this] { return exportAudio(); };

    // Band-level references
    refBox.setTooltip ("Level reference to compare the bands with (saved on this computer, shared by every project)");
    refBox.onChange = [this]
    {
        processor.setSelectedReference (refBox.getSelectedId() > 1 ? refBox.getText() : String());
        updateLevelStrips();
    };
    addAndMakeVisible (refBox);
    saveRefButton.setTooltip ("Save the four bands' levels of this kick as a named reference (an existing name is replaced)");
    saveRefButton.onClick = [this] { saveReference(); };
    addAndMakeVisible (saveRefButton);
    deleteRefButton.setTooltip ("Delete the selected reference");
    deleteRefButton.onClick = [this]
    {
        const auto name = processor.getSelectedReference();
        if (name.isEmpty())
            return;
        AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Delete level reference",
                                      "Delete the reference \"" + name + "\"?", "Delete", "Cancel", this,
                                      ModalCallbackFunction::create ([this, name] (int result)
                                      {
                                          if (result == 0)
                                              return;
                                          references->remove (name);
                                          processor.setSelectedReference ({});
                                          refreshReferenceList();
                                      }));
    };
    addAndMakeVisible (deleteRefButton);
    matchLevelsButton.setTooltip ("Set each band's Level so its RMS matches the selected reference");
    matchLevelsButton.onClick = [this] { matchLevels(); };
    addAndMakeVisible (matchLevelsButton);
    references->addChangeListener (this);
    refreshReferenceList();
    addAndMakeVisible (dragAudio);
    outLengthLcd.setTooltip ("How long the hit lasts (until it stays below -60 dBFS), in beats at the current tempo");
    outPeakLcd.setTooltip ("Peak level of one full-velocity hit");
    renderer.onRendered = [this] (std::shared_ptr<const RenderedHit> hit)
    {
        lastHit = hit;
        updateLevelStrips();
        outputView.setHit (hit);
        const double beatSeconds = 60.0 / jmax (1.0f, processor.getTempo());
        outLengthLcd.setContent ("LENGTH", String (hit->endSeconds / beatSeconds, 2) + " BEATS");
        outPeakLcd.setContent ("PEAK", formatDb (Decibels::gainToDecibels (hit->peak, -60.0f)));
    };

    // Bands: one page at a time in the big window ------------------------------------------------
    tabs.onSelect = [this] (int band) { showPage (band); };
    addAndMakeVisible (tabs);
    for (int b = 0; b < kNumBands; ++b)
    {
        bands[(size_t) b] = std::make_unique<BandControls> (processor, b);
        setupBand (*bands[(size_t) b]);

        const auto name = getBandNames()[b];
        bandBrowsers[(size_t) b] = std::make_unique<BrowserPanel> (
            processor.getSampleLibrary(), processor.getBrowserState (b), bank.getSupportedWildcard(), nullptr,
            browserOptions (name + " Band Browser  -  loads into the " + name + " band only"));
        auto& browser = *bandBrowsers[(size_t) b];
        browser.onLoad = [this, b] (const File& f) { processor.loadFromBrowser (b, f); };
        browser.getCurrentSample = [this, b] { return bank.getEffectiveFile (b); };
        browser.onClose = [this] { showBrowser (-1); };
        addChildComponent (browser);
    }

    // Tail ------------------------------------------------------------------------------------
    addToggle (tailOnButton, ParamIDs::tailOn,
               "Tail: from Start, the Sub band hands over to a pure sine that continues the kick's own tone "
               "(same phase and level) and glides to the Key. Its length follows the Sub band's envelope: "
               "set the Sub Length to Sync 1/2 or 1 Bar for a long tail");
    addToggle (tuneBodyButton, ParamIDs::tuneBody,
               "Tune Body: re-pitch the whole kick so its detected pitch lands on the Key (nearest octave)");
    addToggle (keyLockButton, ParamIDs::keyLock,
               "Key Lock: every MIDI note sets the key, snapped to the Root / Scale (in the octave nearest the Key knob). "
               "Play a pattern up and down the scale in your DAW and the tail follows it; with Tune Body on, "
               "the body is re-pitched to each note too");
    matchButton.setTooltip ("Set the Key to the kick's detected pitch (snapped to the scale)");
    matchButton.onClick = [this] { matchTailKeyToKick(); };
    addAndMakeVisible (matchButton);

    addKnob (keyKnob, ParamIDs::tailKey, [this] (float v)
    {
        const auto note = snapToScale (roundToInt (v),
                                       roundToInt (processor.apvts.getRawParameterValue (ParamIDs::tailRoot)->load()),
                                       roundToInt (processor.apvts.getRawParameterValue (ParamIDs::tailScale)->load()));
        return noteName (note) + "  (" + String (noteToHz (note), 1) + " Hz)";
    }, Unit::Note);
    keyKnob.setTooltip ("The tail's key. It locks to the notes of the Root / Scale");
    addKnob (tailStartKnob, ParamIDs::tailStart, formatMs, Unit::Ms);
    tailStartKnob.inactiveHint = "Tail Start is synced to the note (switch to ms to set it here)";
    addAndMakeVisible (tailStartModeButtons);
    addCombo (tailStartSyncBox, ParamIDs::tailStartSync, getAttackSyncNames(), "Synced Tail Start, at the host tempo");
    addKnob (glideKnob, ParamIDs::tailGlide, formatMs, Unit::Ms);
    addKnob (tailLevelKnob, ParamIDs::tailLevel, formatDb, Unit::Db);
    addCombo (rootBox, ParamIDs::tailRoot, getKeyNames(), "Scale root: the Key knob locks to this scale");
    addCombo (scaleBox, ParamIDs::tailScale, getScaleNames(), "Scale: the Key knob locks to its notes");
    addAndMakeVisible (keyLcd);
    addAndMakeVisible (detectLcd);
    detectLcd.setTooltip ("The pitch detected in the Sub band of the kick");

    // Saturation ----------------------------------------------------------------------------
    addToggle (satOnButton, ParamIDs::satOn, "Saturation on/off");
    addToggle (satOversampleButton, ParamIDs::satOversample,
               "4x oversampling for the saturation (linear-phase filters): less aliasing, more CPU. "
               "Off runs it at the host rate. The timing is the same either way");
    addCombo (satTypeBox, ParamIDs::satType, getSatTypeNames(), "Saturation type");
    addKnob (driveKnob, ParamIDs::satDrive, formatDb, Unit::Db);
    addKnob (toneKnob, ParamIDs::satTone, [] (float v) { return v >= 99.5f ? String ("Open") : formatHz (1000.0f * std::pow (20.0f, v * 0.01f)); });
    toneKnob.parseValue = [] (const String& text) -> double
    {
        // typed as a frequency (1 kHz .. 20 kHz) or "open"; a plain small number is a percentage
        if (text.trim().toLowerCase().startsWith ("open")) return 100.0;
        const double hz = parseValueText (text, Unit::Hz);
        if (std::isnan (hz)) return hz;
        if (hz <= 100.0 && ! text.containsIgnoreCase ("hz") && ! text.containsIgnoreCase ("k")) return hz;
        return 100.0 * std::log (jlimit (1000.0, 20000.0, hz) / 1000.0) / std::log (20.0);
    };
    addKnob (satMixKnob, ParamIDs::satMix, formatPercent, Unit::Percent);
    addKnob (satLevelKnob, ParamIDs::satLevel, formatDb, Unit::Db);
    addAndMakeVisible (driveLcd);
    addAndMakeVisible (satCurve);

    // Compressor ------------------------------------------------------------------------------
    addToggle (compOnButton, ParamIDs::compOn, "Clean compressor on/off (feed-forward, soft knee, stereo linked)");
    addAndMakeVisible (grMeter);
    addKnob (threshKnob, ParamIDs::compThresh, formatDb, Unit::Db);
    addKnob (ratioKnob, ParamIDs::compRatio, [] (float v) { return String (v, v < 10.0f ? 1 : 0) + ":1"; }, Unit::Ratio);
    addKnob (compAttackKnob, ParamIDs::compAttack, formatMs, Unit::Ms);
    addKnob (compReleaseKnob, ParamIDs::compRelease, formatMs, Unit::Ms);
    addKnob (makeupKnob, ParamIDs::compMakeup, formatDb, Unit::Db);
    addKnob (compMixKnob, ParamIDs::compMix, formatPercent, Unit::Percent);
    addAndMakeVisible (threshLcd);

    // Master ------------------------------------------------------------------------------------
    addKnob (volumeKnob, ParamIDs::volume, formatDb, Unit::Db);
    addKnob (velKnob, ParamIDs::velSens, formatPercent, Unit::Percent);
    velKnob.setTooltip ("Velocity sensitivity");
    addAndMakeVisible (volumeLcd);
    addAndMakeVisible (tempoField);

    // Main browser --------------------------------------------------------------------------------
    globalBrowser.onLoad = [this] (const File& f) { processor.loadFromBrowser (KickDrumAudioProcessor::kGlobalBrowser, f); };
    globalBrowser.getCurrentSample = [this] { return bank.getGlobalFile(); };
    globalBrowser.onClose = [this] { showBrowser (-1); };
    addChildComponent (globalBrowser);

    showPage (processor.getSelectedBand());
    showBrowser (processor.getOpenBrowser());
    updateDisplays();
}

KickDrumAudioProcessorEditor::Content::~Content()
{
    references->removeChangeListener (this);
}

void KickDrumAudioProcessorEditor::Content::changeListenerCallback (ChangeBroadcaster*)
{
    refreshReferenceList();
}

void KickDrumAudioProcessorEditor::Content::refreshReferenceList()
{
    const auto selected = processor.getSelectedReference();
    refBox.clear (dontSendNotification);
    refBox.addItem ("No reference", 1);
    int id = 2, selectedId = 1;
    for (auto& r : references->getAll())
    {
        if (r.name.equalsIgnoreCase (selected))
            selectedId = id;
        refBox.addItem (r.name, id++);
    }
    refBox.setSelectedId (selectedId, dontSendNotification);
    updateLevelStrips();
}

void KickDrumAudioProcessorEditor::Content::updateLevelStrips()
{
    LevelReferences::Reference ref;
    const bool hasRef = processor.getSelectedReference().isNotEmpty() && references->find (processor.getSelectedReference(), ref);
    deleteRefButton.setEnabled (hasRef);
    matchLevelsButton.setEnabled (hasRef && lastHit != nullptr);

    if (lastHit == nullptr)
        return;
    for (int b = 0; b < kNumBands; ++b)
        bands[(size_t) b]->levelStrip.setLevel (lastHit->bands[(size_t) b], hasRef,
                                                hasRef && ref.active[(size_t) b], ref.rmsDb[(size_t) b]);
}

void KickDrumAudioProcessorEditor::Content::saveReference()
{
    if (lastHit == nullptr)
        return;

    const auto info = bank.getDisplayInfo();
    const auto suggested = processor.getSelectedReference().isNotEmpty() ? processor.getSelectedReference()
                                                                         : info.global.name;
    auto* window = new AlertWindow ("Save level reference",
                                    "Name for the four band levels of this kick. Using an existing name replaces it.",
                                    MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", suggested, "Name");
    window->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    window->enterModalState (true, ModalCallbackFunction::create ([this, window, hit = lastHit] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;

        LevelReferences::Reference ref;
        ref.name = name;
        for (int b = 0; b < kNumBands; ++b)
        {
            ref.rmsDb[(size_t) b] = hit->bands[(size_t) b].rmsDb;
            ref.peakDb[(size_t) b] = hit->bands[(size_t) b].peakDb;
            ref.active[(size_t) b] = hit->bands[(size_t) b].active;
        }
        references->save (ref);
        processor.setSelectedReference (name);
        refreshReferenceList();
    }), true);
}

void KickDrumAudioProcessorEditor::Content::matchLevels()
{
    LevelReferences::Reference ref;
    if (lastHit == nullptr || ! references->find (processor.getSelectedReference(), ref))
        return;

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& now = lastHit->bands[(size_t) b];
        if (! now.active || ! ref.active[(size_t) b] || now.rmsDb <= -99.0f || ref.rmsDb[(size_t) b] <= -99.0f)
            continue;

        auto* level = processor.apvts.getParameter (bandParam (b, BandParams::level));
        const float current = level->convertFrom0to1 (level->getValue());
        const float target = jlimit (-60.0f, 12.0f, std::round ((current + ref.rmsDb[(size_t) b] - now.rmsDb) * 10.0f) / 10.0f);
        level->beginChangeGesture();
        level->setValueNotifyingHost (level->convertTo0to1 (target));
        level->endChangeGesture();
    }
}

//==============================================================================
void KickDrumAudioProcessorEditor::Content::addKnob (VirusKnob& knob, const String& id, std::function<String (float)> describe,
                                                     Unit unit)
{
    knob.describeValue = std::move (describe);
    knob.parseValue = [unit] (const String& text) { return parseValueText (text, unit); };
    knobAttachments.push_back (std::make_unique<KnobAttachment> (*processor.apvts.getParameter (id), knob));
    addAndMakeVisible (knob);
}

void KickDrumAudioProcessorEditor::Content::addToggle (TextButton& button, const String& id, const String& tooltip)
{
    button.setClickingTogglesState (true);
    button.setTooltip (tooltip);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.apvts, id, button));
    addAndMakeVisible (button);
}

void KickDrumAudioProcessorEditor::Content::addCombo (ComboBox& box, const String& id, const StringArray& items, const String& tooltip)
{
    box.addItemList (items, 1);
    box.setTooltip (tooltip);
    comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.apvts, id, box));
    addAndMakeVisible (box);
}

void KickDrumAudioProcessorEditor::Content::setupBand (BandControls& c)
{
    const int b = c.band;
    const auto name = getBandNames()[b];

    c.view.isSupported = [this] (const File& f) { return bank.isSupportedFile (f); };
    c.view.onFileDropped = [this, b] (const File& f) { bank.loadBand (b, f, processor.getBrowserState (b).preview); };
    c.view.onAudition = [this, b] { processor.audition ((uint8) (1 << b)); };
    c.view.onBrowse = [this, b] { showBrowser (b); };
    addAndMakeVisible (c.view);
    addAndMakeVisible (c.levelStrip);
    for (auto& l : c.valueLcds)
        addAndMakeVisible (l);

    // mini browser
    c.sampleLcd.setTooltip ("The kick this band plays. MAIN = the main kick, OWN = this band's own sample");
    addAndMakeVisible (c.sampleLcd);
    c.star.setTooltip ("Add this band's kick to your favourites (or remove it)");
    c.star.onClick = [this, b]
    {
        const auto f = bank.getEffectiveFile (b);
        if (f.existsAsFile())
            processor.getSampleLibrary().toggleFavourite (f);
        updateDisplays();
    };
    addAndMakeVisible (c.star);
    c.back.setTooltip ("Previous kick in the " + name + " browser, into the " + name + " band only");
    c.next.setTooltip ("Next kick in the " + name + " browser, into the " + name + " band only");
    c.back.onClick = [this, b] { bandBrowsers[(size_t) b]->step (-1); };
    c.next.onClick = [this, b] { bandBrowsers[(size_t) b]->step (+1); };
    addAndMakeVisible (c.back);
    addAndMakeVisible (c.next);
    c.browse.setClickingTogglesState (true);
    c.browse.setTooltip ("Browse for a different kick for the " + name + " band (with audition)");
    c.browse.onClick = [this, b] { showBrowser (bands[(size_t) b]->browse.getToggleState() ? b : -1); };
    addAndMakeVisible (c.browse);
    c.useMain.setTooltip ("Go back to the main kick for this band");
    c.useMain.onClick = [this, b] { bank.clearBand (b); };
    addAndMakeVisible (c.useMain);

    // envelope + level
    addKnob (c.attack, bandParam (b, BandParams::attack), formatMs, Unit::Ms);
    addKnob (c.length, bandParam (b, BandParams::length), formatMs, Unit::Ms);
    addKnob (c.decay, bandParam (b, BandParams::decay), formatMs, Unit::Ms);
    addKnob (c.curve, bandParam (b, BandParams::curve), formatCurve, Unit::Curve);
    c.curve.getProperties().set ("bipolar", true);
    addKnob (c.fade, bandParam (b, BandParams::fade), formatMs, Unit::Ms);
    c.fade.setTooltip ("Fade-out before the end of the sample (the End point), so it never stops with a click");
    addKnob (c.level, bandParam (b, BandParams::level), formatDb, Unit::Db);
    c.length.setTooltip ("How long the band lasts in total (attack, full level, then the decay fade)");
    c.length.inactiveHint = "Length is synced to the note (switch to ms to set it here)";
    c.decay.setTooltip ("The fade at the end of the band. It can't be longer than the Length");
    c.curve.setTooltip ("Decay curve. Right: exponential (drops fast, then a long soft tail). Centre: linear. "
                       "Left: slow start (holds its level, then falls). Every setting fades smoothly to silence");

    addAndMakeVisible (c.attackModeButtons);
    addCombo (c.attackSyncBox, bandParam (b, BandParams::attackSync), getAttackSyncNames(),
              "Synced attack time, at the host tempo");
    c.attack.inactiveHint = "Attack is synced to the note (switch to ms to set it here)";
    addAndMakeVisible (c.modeButtons);
    addCombo (c.syncBox, bandParam (b, BandParams::sync), getSyncNames(),
              "Synced length of the whole band, at the host tempo");
    addToggle (c.mute, bandParam (b, BandParams::mute), "Mute the " + name + " band");
    addToggle (c.solo, bandParam (b, BandParams::solo), "Solo the " + name + " band");
    addToggle (c.phase, bandParam (b, BandParams::phase),
               "Flip the polarity (phase) of the " + name + " band. It glides over a few milliseconds, so it never clicks");
}

//==============================================================================
void KickDrumAudioProcessorEditor::Content::browseForFile()
{
    fileChooser = std::make_unique<FileChooser> ("Load a kick", File::getSpecialLocation (File::userMusicDirectory),
                                                 bank.getSupportedWildcard());
    fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  if (const auto file = fc.getResult(); file.existsAsFile())
                                      processor.loadGlobalFromUser (file);
                              });
}

void KickDrumAudioProcessorEditor::Content::showPage (int band)
{
    band = jlimit (0, kNumBands - 1, band);
    processor.setSelectedBand (band);
    tabs.setSelected (band);
    crossover.setSelectedBand (band);
    for (int b = 0; b < kNumBands; ++b)
        for (auto* c : bands[(size_t) b]->components())
            c->setVisible (b == band);
    repaint();      // the band panel's title
}

void KickDrumAudioProcessorEditor::Content::showBrowser (int index)
{
    if (index != KickDrumAudioProcessor::kGlobalBrowser && ! isPositiveAndBelow (index, kNumBands))
        index = -1;

    processor.setOpenBrowser (index);
    globalBrowser.setVisible (index == KickDrumAudioProcessor::kGlobalBrowser);
    browserButton.setToggleState (index == KickDrumAudioProcessor::kGlobalBrowser, dontSendNotification);

    for (int b = 0; b < kNumBands; ++b)
    {
        bandBrowsers[(size_t) b]->setVisible (index == b);
        bands[(size_t) b]->browse.setToggleState (index == b, dontSendNotification);
    }

    if (index == KickDrumAudioProcessor::kGlobalBrowser)
        globalBrowser.toFront (false);
    else if (index >= 0)
        bandBrowsers[(size_t) index]->toFront (false);
}

void KickDrumAudioProcessorEditor::Content::matchTailKeyToKick()
{
    const auto info = bank.getDisplayInfo();
    if (! info.pitch.isValid())
        return;

    int note = roundToInt (hzToNote (info.pitch.hz));
    while (note < kTailKeyMin) note += 12;
    while (note > kTailKeyMax) note -= 12;

    const int root = roundToInt (processor.apvts.getRawParameterValue (ParamIDs::tailRoot)->load());
    const int scale = roundToInt (processor.apvts.getRawParameterValue (ParamIDs::tailScale)->load());
    note = jlimit (kTailKeyMin, kTailKeyMax, snapToScale (note, root, scale));

    if (auto* param = processor.apvts.getParameter (ParamIDs::tailKey))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) note));
}

//==============================================================================
void KickDrumAudioProcessorEditor::Content::updateDisplays()
{
    auto raw = [this] (const String& id) { return processor.apvts.getRawParameterValue (id)->load(); };

    const auto info = bank.getDisplayInfo();
    const auto settings = processor.makeEngineSettings();
    const double ratio = tuneRatio (settings.tail, info.pitch);
    const double playhead = processor.getPlayheadSeconds();

    mainWave.setInfo (info);
    mainWave.setPlayhead (playhead, ratio);

    outputView.setTiming (processor.getTempo(), kOutputViewBeats);
    outputView.setPlayhead (playhead);
    requestRenderIfNeeded (info);

    // the crossover as the bands hear it
    kd::dsp::CrossoverSettings xs;
    xs.freqs = { raw (ParamIDs::xover1), raw (ParamIDs::xover2), raw (ParamIDs::xover3) };
    xs.slopeIndex = roundToInt (raw (ParamIDs::slope));
    xs = xs.sanitised (48000.0);

    // all four views share one time scale, and the loudest band fills its view
    double span = 0.15;
    float loudest = 0.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        span = jmax (span, settings.bands[(size_t) b].length());
        if (const auto& ov = info.bandOverviews[(size_t) b])
            loudest = jmax (loudest, ov->peak);
    }
    if (info.globalOverview != nullptr)
        span = jmin (span, jmax (info.globalOverview->seconds / ratio, 0.5) + 0.5);
    span = jlimit (0.15, 8.0, span * 1.04);

    const int tailNote = processor.getTailNote();
    const double beatSeconds = 60.0 / jmax (1.0f, processor.getTempo());

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& c = *bands[(size_t) b];
        const auto& env = settings.bands[(size_t) b];
        const bool synced = roundToInt (raw (bandParam (b, BandParams::decayMode))) == 1;

        kd::ui::BandView::State s;
        s.overview = info.bandOverviews[(size_t) b];
        s.env = env;
        {
            // each page fits its own band: the whole Length, or the sample if it ends sooner (no tail)
            double audible = env.length();
            if (! (b == 0 && settings.tail.on) && info.globalOverview != nullptr)
                audible = jmin (audible, info.bands[(size_t) b].seconds * (settings.endFrac - settings.startFrac) / ratio + 0.05);
            s.spanSeconds = jlimit (0.1, 8.0, audible * 1.05);
        }
        juce::ignoreUnused (span);
        s.ghost = info.globalOverview;
        s.beatGrid = synced;
        s.beatSeconds = beatSeconds;
        s.ratio = ratio;
        s.startFrac = settings.startFrac;
        s.endFrac = settings.endFrac;
        s.displayScale = loudest > 0.0f ? 0.95f / loudest : 1.0f;
        s.tailOn = b == 0 && settings.tail.on && info.pitch.isValid();
        s.tailStart = jmax (KickEngine::kTailCrossfade, settings.tail.start);
        s.tailLevel = settings.tail.level;
        s.tailText = "TAIL " + noteName (settings.tail.tuneBody ? roundToInt (hzToNote (info.pitch.hz * ratio)) : tailNote);
        s.rangeText = rangeText (b, xs);
        const int syncIndex = roundToInt (raw (bandParam (b, BandParams::sync)));
        s.lengthText = synced ? getSyncNames()[syncIndex] + "  " + formatMs ((float) (syncBeats (syncIndex) * beatSeconds * 1000.0))
                              : formatMs ((float) (env.length() * 1000.0));
        s.enabled = env.enabled;
        s.inverted = env.invert;
        c.view.setState (s);
        c.view.setPlayhead (playhead);

        {
            auto knobs = c.knobs();
            for (size_t k = 0; k < knobs.size(); ++k)
            {
                c.valueLcds[k].setContent ({}, knobs[k]->getValueText());
                c.valueLcds[k].setDimmed (! knobs[k]->isEnabled());
            }
        }

        // the page tab
        {
            kd::ui::BandTabs::Tab tab;
            // compact range for the tab, e.g. "120 - 1k Hz"
            auto shortHz = [] (float hz) { return hz >= 1000.0f ? String (hz / 1000.0f, hz >= 10000.0f || std::fmod (hz, 1000.0f) < 1.0f ? 0 : 1) + "k"
                                                                : String (roundToInt (hz)); };
            const float edges[] { 20.0f, xs.freqs[0], xs.freqs[1], xs.freqs[2] };
            tab.range = b < 3 ? shortHz (edges[b]) + " - " + shortHz (b == 0 ? xs.freqs[0] : xs.freqs[(size_t) b]) + " Hz"
                              : shortHz (xs.freqs[2]) + " Hz +";
            LevelReferences::Reference ref;
            tab.hasRef = processor.getSelectedReference().isNotEmpty() && references->find (processor.getSelectedReference(), ref)
                         && ref.active[(size_t) b];
            if (lastHit != nullptr)
            {
                tab.measured = true;
                tab.active = lastHit->bands[(size_t) b].active;
                tab.rmsDb = lastHit->bands[(size_t) b].rmsDb;
                tab.refDiff = tab.rmsDb - ref.rmsDb[(size_t) b];
            }
            tabs.setTab (b, tab);
        }

        c.length.setEnabled (! synced);
        c.syncBox.setEnabled (synced);
        const bool attackSynced = roundToInt (raw (bandParam (b, BandParams::attackMode))) == 1;
        c.attack.setEnabled (! attackSynced);
        c.attackSyncBox.setEnabled (attackSynced);

        // mini browser
        const auto& src = info.bands[(size_t) b];
        const bool own = info.overridden[(size_t) b];
        String name = src.name.isEmpty() ? String ("-") : src.name;
        if (src.missing) name << " (missing)";
        c.sampleLcd.setContent (name.toUpperCase(), own ? "OWN" : "MAIN");
        c.sampleLcd.setDimmed (! own);
        const auto file = bank.getEffectiveFile (b);
        c.star.setEnabled (file.existsAsFile());
        c.star.setToggleState (file.existsAsFile() && processor.getSampleLibrary().isFavourite (file), dontSendNotification);
        c.useMain.setEnabled (own);
        c.back.setEnabled (bandBrowsers[(size_t) b]->hasFiles());
        c.next.setEnabled (bandBrowsers[(size_t) b]->hasFiles());
    }

    // top bar
    {
        const auto file = bank.getGlobalFile();
        favouriteButton.setEnabled (file.existsAsFile());
        favouriteButton.setToggleState (file.existsAsFile() && processor.getSampleLibrary().isFavourite (file), dontSendNotification);
        backButton.setEnabled (globalBrowser.hasFiles());
        nextButton.setEnabled (globalBrowser.hasFiles());
        if (presetBox.getSelectedId() != processor.getCurrentProgram() + 1 && ! presetBox.isPopupActive())
            presetBox.setSelectedId (processor.getCurrentProgram() + 1, dontSendNotification);
    }

    // tail
    {
        const bool locked = raw (ParamIDs::keyLock) > 0.5f;
        const int lastNote = processor.getLastLockedNote();
        if (locked && lastNote >= 0)
            keyLcd.setContent ("MIDI", noteName (lastNote));
        else
            keyLcd.setContent ("KEY", noteName (tailNote));
    }
    keyLcd.setDimmed (! settings.tail.on && ! settings.tail.tuneBody);
    if (info.pitch.isValid())
    {
        const double note = hzToNote (info.pitch.hz);
        const int n = roundToInt (note);
        const int cents = roundToInt ((note - n) * 100.0);
        detectLcd.setContent ("KICK", noteName (n) + (cents == 0 ? String() : String (cents > 0 ? " +" : " ") + String (cents) + " ct"));
    }
    else
    {
        detectLcd.setContent ("KICK", "NO PITCH");
    }
    matchButton.setEnabled (info.pitch.isValid());
    glideKnob.setEnabled (! settings.tail.tuneBody);
    {
        const bool startSynced = raw (ParamIDs::tailStartMode) > 0.5f;
        tailStartKnob.setEnabled (! startSynced);
        tailStartSyncBox.setEnabled (startSynced);
    }
    glideKnob.inactiveHint = "With Tune Body the kick is already in the key, so the tail doesn't glide";

    // saturation
    const bool satOn = raw (ParamIDs::satOn) > 0.5f;
    satCurve.setState (satOn, roundToInt (raw (ParamIDs::satType)), raw (ParamIDs::satDrive), raw (ParamIDs::satMix) * 0.01f);
    driveLcd.setContent ("DRIVE", formatDb (raw (ParamIDs::satDrive)));
    driveLcd.setDimmed (! satOn);

    // compressor
    const bool compOn = raw (ParamIDs::compOn) > 0.5f;
    grMeter.setValue (processor.getGainReductionDb(), compOn);
    threshLcd.setContent ("THRES", formatDb (raw (ParamIDs::compThresh)));
    threshLcd.setDimmed (! compOn);

    // master
    volumeLcd.setContent ("VOLUME", formatDb (raw (ParamIDs::volume)));
    tempoField.setTempo (processor.getTempo(), processor.isTempoFromHost());
}

void KickDrumAudioProcessorEditor::Content::requestRenderIfNeeded (const KickBank::DisplayInfo& info)
{
    // re-render whenever anything that changes the sound (or the view length) changes
    int64 key = (int64) reinterpret_cast<pointer_sized_int> (info.set.get());
    for (auto* p : processor.getParameters())
        key = key * 1000003 + (int64) (p->getValue() * 1.0e6f);
    key = key * 31 + roundToInt (processor.getTempo() * 100.0f);
    key = key * 31 + roundToInt (processor.getRenderSampleRate());

    if (key == lastRenderKey || info.set == nullptr)
        return;
    lastRenderKey = key;

    auto r = makeRenderRequest (info);
    r.measureBands = true;
    renderer.request (r);
}

OutputRenderer::Request KickDrumAudioProcessorEditor::Content::makeRenderRequest (const KickBank::DisplayInfo& info) const
{
    OutputRenderer::Request r;
    r.set = info.set;
    r.engine = processor.makeEngineSettings();
    r.saturation = processor.makeSaturationSettings();
    r.compressor = processor.makeCompressorSettings();
    r.volume = processor.getVolumeGain();
    r.sampleRate = processor.getRenderSampleRate();
    const double beatSeconds = 60.0 / jmax (1.0f, processor.getTempo());
    // render the whole hit (so its length can be measured), the display shows the first 1/4 note
    double longest = 0.0;
    for (auto& b : r.engine.bands)
        longest = jmax (longest, b.length());
    r.seconds = jlimit (kOutputViewBeats * beatSeconds, 8.0, longest + 0.2);
    return r;
}

File KickDrumAudioProcessorEditor::Content::exportAudio()
{
    const auto info = bank.getDisplayInfo();
    if (info.set == nullptr)
        return {};

    // render now, so the file is exactly the current sound
    const auto hit = OutputRenderer::render (makeRenderRequest (info));

    const auto name = File::createLegalFileName (info.global.name.isEmpty() ? String ("Kick") : info.global.name);
    const auto stamp = Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto folder = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("KickDrum").getChildFile ("Exports");
    // a new file every time: the DAW may keep referring to files it was given earlier
    const auto file = folder.getNonexistentChildFile ("KickDrum " + name + " " + noteName (processor.getTailNote())
                                                          + " " + String (roundToInt (processor.getTempo())) + "BPM " + stamp, ".wav", false);
    return OutputRenderer::writeWav (*hit, file) ? file : File();
}

//==============================================================================
void KickDrumAudioProcessorEditor::Content::resized()
{
    // Top bar
    presetBox.setBounds (528, 10, 250, 28);
    favouriteButton.setBounds (784, 10, 28, 28);
    backButton.setBounds (816, 10, 28, 28);
    nextButton.setBounds (846, 10, 28, 28);
    browserButton.setBounds (880, 10, 90, 28);
    loadButton.setBounds (976, 10, 98, 28);
    playButton.setBounds (1080, 10, 88, 28);

    // Left column: kick, crossover, tail ---------------------------------------------------------
    kickPanel  = { 12.0f, 54.0f, 300.0f, 168.0f };
    xoverPanel = { 12.0f, 230.0f, 300.0f, 176.0f };
    tailPanel  = { 12.0f, 414.0f, 300.0f, 208.0f };

    mainWave.setBounds (20, 78, 284, 104);
    refBox.setBounds (20, 188, 128, 24);
    saveRefButton.setBounds (152, 188, 48, 24);
    deleteRefButton.setBounds (204, 188, 40, 24);
    matchLevelsButton.setBounds (248, 188, 56, 24);

    crossover.setBounds (20, 254, 284, 92);
    for (int i = 0; i < 3; ++i)
        xoverFields[(size_t) i]->setBounds (20 + i * 96, 352, 92, 22);
    slopeButtons.setBounds (70, 380, 234, 22);

    {
        const int x = 12, y = 414;
        tailOnButton.setBounds (x + 8, y + 26, 66, 24);
        tuneBodyButton.setBounds (x + 78, y + 26, 74, 24);
        keyLockButton.setBounds (x + 156, y + 26, 70, 24);
        matchButton.setBounds (x + 230, y + 26, 62, 24);
        keyKnob.setBounds (x + 20, y + 56, 76, 76);
        keyLcd.setBounds (x + 10, y + 136, 94, 22);
        tailStartModeButtons.setBounds (x + 10, y + 162, 94, 20);
        tailStartSyncBox.setBounds (x + 10, y + 185, 94, 20);
        rootBox.setBounds (x + 110, y + 66, 58, 22);
        scaleBox.setBounds (x + 172, y + 66, 120, 22);
        detectLcd.setBounds (x + 110, y + 94, 182, 22);
        tailStartKnob.setBounds (x + 110, y + 124, 60, 76);
        glideKnob.setBounds (x + 171, y + 124, 60, 76);
        tailLevelKnob.setBounds (x + 232, y + 124, 60, 76);
    }

    // Centre: page tabs, the big window, the band's envelope and level ---------------------------
    tabs.setBounds (320, 54, 540, 42);
    bigWindow = { 320.0f, 96.0f, 540.0f, 300.0f };
    bandPanel = { 320.0f, 404.0f, 540.0f, 218.0f };

    for (auto& bp : bands)
    {
        auto& c = *bp;
        c.view.setBounds (330, 106, 520, 230);
        c.levelStrip.setBounds (330, 342, 520, 20);
        c.sampleLcd.setBounds (330, 368, 250, 24);
        c.star.setBounds (586, 368, 26, 24);
        c.back.setBounds (616, 368, 26, 24);
        c.next.setBounds (644, 368, 26, 24);
        c.browse.setBounds (676, 368, 84, 24);
        c.useMain.setBounds (764, 368, 86, 24);

        const int px = 320, py = 404;
        auto knobs = c.knobs();
        for (int k = 0; k < 6; ++k)
        {
            const int kx = px + 52 + k * 87;
            knobs[(size_t) k]->setBounds (kx - 34, py + 26, 68, 68);
            c.valueLcds[(size_t) k].setBounds (kx - 40, py + 114, 80, 22);
        }

        const int yr = py + 150;
        c.attackModeButtons.setBounds (px + 66, yr, 88, 24);
        c.attackSyncBox.setBounds (px + 158, yr, 64, 24);
        c.modeButtons.setBounds (px + 290, yr, 88, 24);
        c.syncBox.setBounds (px + 382, yr, 70, 24);
        c.mute.setBounds (px + 458, yr, 36, 24);
        c.solo.setBounds (px + 498, yr, 36, 24);
        c.phase.setBounds (px + 458, yr + 30, 76, 24);
    }

    // Right column: output, master -------------------------------------------------------------
    outputPanel = { 868.0f, 54.0f, 300.0f, 230.0f };
    masterPanel = { 868.0f, 292.0f, 300.0f, 330.0f };

    outputView.setBounds (876, 78, 284, 120);
    outLengthLcd.setBounds (876, 206, 140, 24);
    outPeakLcd.setBounds (1020, 206, 140, 24);
    dragAudio.setBounds (876, 238, 284, 28);

    volumeKnob.setBounds (966, 326, 104, 104);
    volumeLcd.setBounds (938, 446, 160, 26);
    velKnob.setBounds (988, 486, 60, 76);
    tempoField.setBounds (928, 576, 180, 26);

    // Bottom row: saturation, compressor ---------------------------------------------------------
    satPanel  = { 12.0f, 630.0f, 572.0f, 188.0f };
    compPanel = { 592.0f, 630.0f, 576.0f, 188.0f };
    {
        const int x = 12, y = 630;
        satOnButton.setBounds (x + 8, y + 26, 54, 24);
        satTypeBox.setBounds (x + 68, y + 26, 200, 24);
        satOversampleButton.setBounds (x + 274, y + 26, 70, 24);
        driveKnob.setBounds (x + 20, y + 58, 84, 84);
        driveLcd.setBounds (x + 14, y + 152, 96, 24);
        satCurve.setBounds (x + 128, y + 60, 160, 116);
        toneKnob.setBounds (x + 310, y + 74, 70, 80);
        satMixKnob.setBounds (x + 390, y + 74, 70, 80);
        satLevelKnob.setBounds (x + 470, y + 74, 70, 80);
    }
    {
        const int x = 592, y = 630;
        compOnButton.setBounds (x + 8, y + 26, 54, 24);
        grMeter.setBounds (x + 68, y + 26, 250, 24);
        threshKnob.setBounds (x + 20, y + 58, 84, 84);
        threshLcd.setBounds (x + 14, y + 152, 96, 24);
        VirusKnob* knobs[] { &ratioKnob, &compAttackKnob, &compReleaseKnob, &makeupKnob, &compMixKnob };
        for (int i = 0; i < 5; ++i)
            knobs[i]->setBounds (x + 124 + i * 88, y + 74, 70, 80);
    }

    // Browser overlays cover everything below the top bar
    const Rectangle<int> browserArea (12, 52, kBaseWidth - 24, kBaseHeight - 52 - 10);
    globalBrowser.setBounds (browserArea);
    for (auto& b : bandBrowsers)
        b->setBounds (browserArea);
}

//==============================================================================
void KickDrumAudioProcessorEditor::Content::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawBackground (g, bounds);
    drawWordmark (g, { 18.0f, 4.0f, 300.0f, 42.0f });

    CustomLookAndFeel::drawSmallLabel (g, "Preset", { 456.0f, 10.0f, 64.0f, 28.0f }, Justification::centredRight);

    CustomLookAndFeel::drawPanel (g, kickPanel, "Kick");
    CustomLookAndFeel::drawPanel (g, xoverPanel, "Crossover  -  Linear Phase");
    CustomLookAndFeel::drawSmallLabel (g, "Slope", { 20.0f, 380.0f, 48.0f, 22.0f }, Justification::centredLeft);

    CustomLookAndFeel::drawPanel (g, tailPanel, "Tail  -  Tuned Extension");
    CustomLookAndFeel::drawSmallLabel (g, "Root", { tailPanel.getX() + 110.0f, tailPanel.getY() + 52.0f, 58.0f, 14.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Scale lock", { tailPanel.getX() + 172.0f, tailPanel.getY() + 52.0f, 120.0f, 14.0f }, Justification::centredLeft);

    // the big window: a panel body without a header (the tabs sit on top of it)
    {
        const auto w = bigWindow;
        g.setColour (VirusColours::bevelLight);
        g.drawRoundedRectangle (w.translated (0.5f, 1.0f), 5.0f, 1.0f);
        g.setGradientFill (ColourGradient (VirusColours::panelTop, 0.0f, w.getY(), VirusColours::panelBottom, 0.0f, w.getBottom(), false));
        g.fillRoundedRectangle (w, 5.0f);
        g.setColour (VirusColours::bevelDark);
        g.drawRoundedRectangle (w, 5.0f, 1.0f);
        // join the selected tab to the window
        const float tabW = w.getWidth() / (float) kNumBands;
        const int page = tabs.getSelected();
        g.setColour (Colour (0xff2a2a2d).interpolatedWith (VirusColours::panelTop, 0.5f));
        g.fillRect (w.getX() + page * tabW + 3.0f, w.getY() - 1.0f, tabW - 6.0f, 3.0f);
    }

    const int page = tabs.getSelected();
    CustomLookAndFeel::drawPanel (g, bandPanel, getBandNames()[page].toUpperCase() + "  -  Envelope & Level");
    g.setColour (kd::ui::bandColour (page));
    g.fillRoundedRectangle (bandPanel.getX() + 8.0f, bandPanel.getY() + 6.0f, 18.0f, 6.0f, 2.0f);
    {
        const char* names[] { "Attack", "Length", "Decay", "Curve", "Fade", "Level" };
        for (int k = 0; k < 6; ++k)
        {
            const float kx = bandPanel.getX() + 52.0f + k * 87.0f;
            CustomLookAndFeel::drawSmallLabel (g, names[k], { kx - 40.0f, bandPanel.getY() + 96.0f, 80.0f, 14.0f });
        }
        CustomLookAndFeel::drawSmallLabel (g, "Attack", { bandPanel.getX() + 12.0f, bandPanel.getY() + 150.0f, 52.0f, 24.0f },
                                           Justification::centredLeft);
        CustomLookAndFeel::drawSmallLabel (g, "Length", { bandPanel.getX() + 236.0f, bandPanel.getY() + 150.0f, 52.0f, 24.0f },
                                           Justification::centredLeft);
    }

    CustomLookAndFeel::drawPanel (g, outputPanel, "Output  -  1/4 Note");
    CustomLookAndFeel::drawPanel (g, masterPanel, "Master");
    CustomLookAndFeel::drawPanel (g, satPanel, "Saturation");
    CustomLookAndFeel::drawPanel (g, compPanel, "Compressor");
}

void KickDrumAudioProcessorEditor::Content::drawBackground (Graphics& g, Rectangle<float> b)
{
    g.setGradientFill (ColourGradient (Colour (0xff1a1a1c), 0.0f, 0.0f, Colour (0xff070708), 0.0f, b.getBottom(), false));
    g.fillRect (b);

    // brushed metal: random short streaks (drawn as vectors so they scale cleanly)
    Random rng (0x6b64);
    for (int i = 0; i < 1800; ++i)
    {
        const float x = rng.nextFloat() * b.getWidth(), y = rng.nextFloat() * b.getHeight();
        const float len = 20.0f + rng.nextFloat() * 140.0f;
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1.0f);
    }

    ColourGradient vignette (Colours::transparentBlack, b.getCentreX(), b.getHeight() * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillRect (b);

    // top strip (logo, presets, buttons), with a black line along its bottom edge
    const auto strip = b.withHeight (46.0f);
    g.setGradientFill (ColourGradient (Colour (0xff222224), 0.0f, strip.getY(), Colour (0xff111112), 0.0f, strip.getBottom(), false));
    g.fillRect (strip);
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (46, 0.0f, b.getWidth());
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (47, 0.0f, b.getWidth());
}

void KickDrumAudioProcessorEditor::Content::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Same family wordmark as Virus Delay / LeadVerb: chrome first word, orange second word, swoosh.
    const float baseline = area.getBottom() - 9.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (30.0f, Font::bold | Font::italic)), "kick", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (21.0f, Font::plain)), "drum", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (VirusColours::accent);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (VirusColours::accent.withAlpha (0.0f), sx, sy, VirusColours::accent, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}
