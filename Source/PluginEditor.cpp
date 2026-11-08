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
      attackModeButtons (*p.apvts.getParameter (bandParam (b, BandParams::attackMode)), { "ms", "Sync" },
                         "Attack in milliseconds, or Sync: the attack lasts a note length (1/64 to 1/4) at the tempo"),
      modeButtons (*p.apvts.getParameter (bandParam (b, BandParams::decayMode)), { "ms", "Sync" },
                   "Length in milliseconds, or Sync: the band lasts exactly a note length at the tempo")
{
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
      crossover (*p.apvts.getParameter (ParamIDs::xover1), *p.apvts.getParameter (ParamIDs::xover2),
                 *p.apvts.getParameter (ParamIDs::xover3), *p.apvts.getParameter (ParamIDs::slope)),
      slopeButtons (*p.apvts.getParameter (ParamIDs::slope), { "6", "12", "18", "24", "36" },
                    "Crossover slope in dB/oct. Gentler slopes ring less; steeper ones keep the bands more separate"),
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
    outLengthLcd.setTooltip ("How long the hit lasts (until it stays below -60 dBFS), in beats at the current tempo");
    outPeakLcd.setTooltip ("Peak level of one full-velocity hit");
    renderer.onRendered = [this] (std::shared_ptr<const RenderedHit> hit)
    {
        outputView.setHit (hit);
        const double beatSeconds = 60.0 / jmax (1.0f, processor.getTempo());
        outLengthLcd.setContent ("LENGTH", String (hit->endSeconds / beatSeconds, 2) + " BEATS");
        outPeakLcd.setContent ("PEAK", formatDb (Decibels::gainToDecibels (hit->peak, -60.0f)));
    };

    // Bands ---------------------------------------------------------------------------------
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
    addKnob (glideKnob, ParamIDs::tailGlide, formatMs, Unit::Ms);
    addKnob (tailLevelKnob, ParamIDs::tailLevel, formatDb, Unit::Db);
    addCombo (rootBox, ParamIDs::tailRoot, getKeyNames(), "Scale root: the Key knob locks to this scale");
    addCombo (scaleBox, ParamIDs::tailScale, getScaleNames(), "Scale: the Key knob locks to its notes");
    addAndMakeVisible (keyLcd);
    addAndMakeVisible (detectLcd);
    detectLcd.setTooltip ("The pitch detected in the Sub band of the kick");

    // Saturation ----------------------------------------------------------------------------
    addToggle (satOnButton, ParamIDs::satOn, "Saturation on/off (4x oversampled, linear-phase filters)");
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

    showBrowser (processor.getOpenBrowser());
    updateDisplays();
}

KickDrumAudioProcessorEditor::Content::~Content() = default;

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
        s.spanSeconds = span;
        s.ratio = ratio;
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
        c.view.setState (s);
        c.view.setPlayhead (playhead);

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
    renderer.request (r);
}

//==============================================================================
void KickDrumAudioProcessorEditor::Content::resized()
{
    // Top bar
    presetBox.setBounds (470, 10, 230, 28);
    favouriteButton.setBounds (706, 10, 28, 28);
    backButton.setBounds (740, 10, 28, 28);
    nextButton.setBounds (770, 10, 28, 28);
    browserButton.setBounds (804, 10, 90, 28);
    loadButton.setBounds (900, 10, 98, 28);
    playButton.setBounds (1004, 10, 84, 28);

    // Kick + crossover
    mainWave.setBounds (12, 54, 640, 108);
    crossover.setBounds (660, 54, 428, 80);
    for (int i = 0; i < 3; ++i)
        xoverFields[(size_t) i]->setBounds (660 + i * 74, 138, 70, 24);
    slopeButtons.setBounds (884, 138, 204, 24);

    // Output (bottom row, under the FX)
    outputView.setBounds (12, 756, 900, 122);
    outLengthLcd.setBounds (920, 790, 168, 26);
    outPeakLcd.setBounds (920, 822, 168, 26);

    // Bands
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& c = *bands[(size_t) b];
        const int x = 12 + b * 271, y = 170;
        c.panel = { (float) x, (float) y, 263.0f, 314.0f };

        c.view.setBounds (x + 8, y + 24, 247, 98);
        c.sampleLcd.setBounds (x + 8, y + 126, 247, 24);
        c.star.setBounds (x + 8, y + 153, 24, 24);
        c.back.setBounds (x + 36, y + 153, 24, 24);
        c.next.setBounds (x + 62, y + 153, 24, 24);
        c.browse.setBounds (x + 90, y + 153, 80, 24);
        c.useMain.setBounds (x + 174, y + 153, 81, 24);

        VirusKnob* knobs[] { &c.attack, &c.length, &c.decay, &c.curve, &c.level };
        for (int k = 0; k < 5; ++k)
            knobs[k]->setBounds (x + 8 + roundToInt (k * 49.4f), y + 182, 49, 68);

        c.attackModeButtons.setBounds (x + 60, y + 256, 76, 24);
        c.attackSyncBox.setBounds (x + 140, y + 256, 74, 24);
        c.mute.setBounds (x + 220, y + 256, 35, 24);
        c.modeButtons.setBounds (x + 60, y + 284, 76, 24);
        c.syncBox.setBounds (x + 140, y + 284, 74, 24);
        c.solo.setBounds (x + 220, y + 284, 35, 24);
    }

    // Bottom row
    tailPanel   = { 12.0f, 492.0f, 300.0f, 256.0f };
    satPanel    = { 320.0f, 492.0f, 300.0f, 256.0f };
    compPanel   = { 628.0f, 492.0f, 300.0f, 256.0f };
    masterPanel = { 936.0f, 492.0f, 152.0f, 256.0f };

    {
        const int x = 12, y = 492;
        tailOnButton.setBounds (x + 10, y + 28, 64, 26);
        tuneBodyButton.setBounds (x + 78, y + 28, 78, 26);
        keyLockButton.setBounds (x + 160, y + 28, 72, 26);
        matchButton.setBounds (x + 236, y + 28, 54, 26);
        keyKnob.setBounds (x + 14, y + 66, 84, 84);
        keyLcd.setBounds (x + 10, y + 156, 94, 26);
        rootBox.setBounds (x + 112, y + 82, 58, 24);
        scaleBox.setBounds (x + 174, y + 82, 116, 24);
        detectLcd.setBounds (x + 112, y + 124, 178, 26);
        tailStartKnob.setBounds (x + 112, y + 168, 60, 72);
        glideKnob.setBounds (x + 172, y + 168, 60, 72);
        tailLevelKnob.setBounds (x + 232, y + 168, 60, 72);
    }
    {
        const int x = 320, y = 492;
        satOnButton.setBounds (x + 10, y + 28, 60, 26);
        satTypeBox.setBounds (x + 76, y + 29, 214, 24);
        driveKnob.setBounds (x + 14, y + 66, 84, 84);
        driveLcd.setBounds (x + 10, y + 156, 94, 26);
        satCurve.setBounds (x + 112, y + 64, 178, 92);
        toneKnob.setBounds (x + 112, y + 168, 60, 72);
        satMixKnob.setBounds (x + 172, y + 168, 60, 72);
        satLevelKnob.setBounds (x + 232, y + 168, 60, 72);
    }
    {
        const int x = 628, y = 492;
        compOnButton.setBounds (x + 10, y + 28, 60, 26);
        grMeter.setBounds (x + 76, y + 29, 214, 24);
        threshKnob.setBounds (x + 14, y + 66, 84, 84);
        threshLcd.setBounds (x + 10, y + 156, 94, 26);
        ratioKnob.setBounds (x + 112, y + 74, 60, 72);
        compAttackKnob.setBounds (x + 172, y + 74, 60, 72);
        compReleaseKnob.setBounds (x + 232, y + 74, 60, 72);
        makeupKnob.setBounds (x + 142, y + 168, 60, 72);
        compMixKnob.setBounds (x + 202, y + 168, 60, 72);
    }
    {
        const int x = 936, y = 492;
        volumeKnob.setBounds (x + 34, y + 28, 84, 84);
        volumeLcd.setBounds (x + 10, y + 118, 132, 26);
        velKnob.setBounds (x + 46, y + 148, 60, 70);
        tempoField.setBounds (x + 10, y + 222, 132, 26);
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

    CustomLookAndFeel::drawSmallLabel (g, "Preset", { 400.0f, 10.0f, 64.0f, 28.0f }, Justification::centredRight);
    CustomLookAndFeel::drawSmallLabel (g, "Output  -  1/4 note", { 920.0f, 762.0f, 168.0f, 16.0f }, Justification::centredLeft);

    const auto names = getBandNames();
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& c = *bands[(size_t) b];
        CustomLookAndFeel::drawPanel (g, c.panel, "Band " + String (b + 1) + "  -  " + names[b]);

        // band colour tag in the header
        g.setColour (kd::ui::bandColour (b));
        g.fillRoundedRectangle (c.panel.getX() + 8.0f, c.panel.getY() + 6.0f, 18.0f, 6.0f, 2.0f);

        CustomLookAndFeel::drawSmallLabel (g, "Attack", { c.panel.getX() + 8.0f, c.panel.getY() + 256.0f, 50.0f, 24.0f },
                                           Justification::centredLeft);
        CustomLookAndFeel::drawSmallLabel (g, "Length", { c.panel.getX() + 8.0f, c.panel.getY() + 284.0f, 50.0f, 24.0f },
                                           Justification::centredLeft);
    }

    CustomLookAndFeel::drawPanel (g, tailPanel, "Tail  -  Tuned Extension");
    CustomLookAndFeel::drawSmallLabel (g, "Root", { tailPanel.getX() + 112.0f, tailPanel.getY() + 66.0f, 58.0f, 14.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Scale lock", { tailPanel.getX() + 174.0f, tailPanel.getY() + 66.0f, 116.0f, 14.0f }, Justification::centredLeft);

    CustomLookAndFeel::drawPanel (g, satPanel, "Saturation");
    CustomLookAndFeel::drawPanel (g, compPanel, "Compressor");
    CustomLookAndFeel::drawPanel (g, masterPanel, "Master");

    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine ((int) compPanel.getY() + 160, compPanel.getX() + 108.0f, compPanel.getRight() - 8.0f);
}

void KickDrumAudioProcessorEditor::Content::drawBackground (Graphics& g, Rectangle<float> b)
{
    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, b.getBottom(), false));
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

    // separator under the top bar
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (46, 0.0f, b.getWidth());
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (47, 0.0f, b.getWidth());
}

void KickDrumAudioProcessorEditor::Content::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Same family wordmark as Virus Delay: chrome first word, blue second word, swoosh.
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
    g.setColour (VirusColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (VirusColours::blue.withAlpha (0.0f), sx, sy, VirusColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}
