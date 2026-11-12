#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;
using namespace kd;

//==============================================================================
struct KickDrumAudioProcessor::ParamPointers
{
    explicit ParamPointers (AudioProcessorValueTreeState& s)
    {
        auto get = [&s] (const String& id)
        {
            auto* p = s.getRawParameterValue (id);
            jassert (p != nullptr);
            return p;
        };

        for (int i = 0; i < 3; ++i)
            xover[(size_t) i] = get ("xover" + String (i + 1));
        slope = get (ParamIDs::slope);

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& band = bands[(size_t) b];
            band.attack    = get (bandParam (b, BandParams::attack));
            band.fade      = get (bandParam (b, BandParams::fade));
            band.attackMode = get (bandParam (b, BandParams::attackMode));
            band.attackSync = get (bandParam (b, BandParams::attackSync));
            band.length    = get (bandParam (b, BandParams::length));
            band.decay     = get (bandParam (b, BandParams::decay));
            band.curve     = get (bandParam (b, BandParams::curve));
            band.level     = get (bandParam (b, BandParams::level));
            band.decayMode = get (bandParam (b, BandParams::decayMode));
            band.sync      = get (bandParam (b, BandParams::sync));
            band.mute      = get (bandParam (b, BandParams::mute));
            band.solo      = get (bandParam (b, BandParams::solo));
        }

        tailOn = get (ParamIDs::tailOn);       tailKey = get (ParamIDs::tailKey);
        keyLock = get (ParamIDs::keyLock);
        tailStartMode = get (ParamIDs::tailStartMode); tailStartSync = get (ParamIDs::tailStartSync);
        sampleStart = get (ParamIDs::sampleStart);     sampleEnd = get (ParamIDs::sampleEnd);
        tailRoot = get (ParamIDs::tailRoot);   tailScale = get (ParamIDs::tailScale);
        tailStart = get (ParamIDs::tailStart); tailGlide = get (ParamIDs::tailGlide);
        tailLevel = get (ParamIDs::tailLevel); tuneBody = get (ParamIDs::tuneBody);

        satOn = get (ParamIDs::satOn);       satType = get (ParamIDs::satType);
        satDrive = get (ParamIDs::satDrive); satTone = get (ParamIDs::satTone);
        satMix = get (ParamIDs::satMix);     satLevel = get (ParamIDs::satLevel);

        compOn = get (ParamIDs::compOn);           compThresh = get (ParamIDs::compThresh);
        compRatio = get (ParamIDs::compRatio);     compAttack = get (ParamIDs::compAttack);
        compRelease = get (ParamIDs::compRelease); compMakeup = get (ParamIDs::compMakeup);
        compMix = get (ParamIDs::compMix);

        velSens = get (ParamIDs::velSens);
        volume = get (ParamIDs::volume);
        bpm = get (ParamIDs::bpm);
    }

    struct Band
    {
        std::atomic<float>* attack, *fade, *attackMode, *attackSync, *length, *decay, *curve, *level, *decayMode, *sync, *mute, *solo;
    };

    std::array<std::atomic<float>*, 3> xover {};
    std::atomic<float>* slope;
    std::array<Band, kNumBands> bands {};
    std::atomic<float>* keyLock, *tailStartMode, *tailStartSync, *sampleStart, *sampleEnd;
    std::atomic<float>* tailOn, *tailKey, *tailRoot, *tailScale, *tailStart, *tailGlide, *tailLevel, *tuneBody;
    std::atomic<float>* satOn, *satType, *satDrive, *satTone, *satMix, *satLevel;
    std::atomic<float>* compOn, *compThresh, *compRatio, *compAttack, *compRelease, *compMakeup, *compMix;
    std::atomic<float>* velSens, *volume, *bpm;
};

namespace
{
    inline float val (const std::atomic<float>* p) noexcept  { return p->load (std::memory_order_relaxed); }
    inline bool on (const std::atomic<float>* p) noexcept    { return val (p) > 0.5f; }
    inline int idx (const std::atomic<float>* p) noexcept    { return roundToInt (val (p)); }
}

//==============================================================================
KickDrumAudioProcessor::KickDrumAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KickDrumState", createParameterLayout())
{
    params = std::make_unique<ParamPointers> (apvts);

    // browsers start in "All Folders", previewing every kick they load
    for (auto& s : browserStates)
        s.place = 1;

    timerCallback();
    startTimerHz (20);
}

KickDrumAudioProcessor::~KickDrumAudioProcessor()
{
    stopTimer();
}

//==============================================================================
void KickDrumAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    fx.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (fx.getLatencySamples());
    volumeSmooth.reset (sampleRate, 0.02);
    volumeSmooth.setCurrentAndTargetValue (Decibels::decibelsToGain (val (params->volume), -60.0f));
}

void KickDrumAudioProcessor::releaseResources()
{
    engine.reset();
    fx.reset();
}

bool KickDrumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

//==============================================================================
EngineSettings KickDrumAudioProcessor::makeEngineSettings() const
{
    EngineSettings s;
    const double beatSeconds = 60.0 / jmax (1.0f, tempo.load());

    bool anySolo = false;
    for (auto& b : params->bands)
        anySolo = anySolo || on (b.solo);

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& p = params->bands[(size_t) i];
        auto& b = s.bands[(size_t) i];
        b.attack = idx (p.attackMode) == 1 ? attackSyncBeats (idx (p.attackSync)) * beatSeconds
                                           : val (p.attack) * 0.001;
        b.curve = val (p.curve) * 0.01f;
        b.fade = val (p.fade) * 0.001;
        b.gain = val (p.level) <= -59.9f ? 0.0f : Decibels::decibelsToGain (val (p.level));
        b.enabled = anySolo ? on (p.solo) : ! on (p.mute);

        // Length (ms, or synced to the note) is the whole band. The Decay is the fade at its
        // end; whatever is left after the Attack holds at full level.
        const double length = idx (p.decayMode) == 1 ? syncBeats (idx (p.sync)) * beatSeconds
                                                     : val (p.length) * 0.001;
        b.attack = jmin (b.attack, length);
        b.decay = jmin (val (p.decay) * 0.001, length - b.attack);
        b.hold = length - b.attack - b.decay;
    }

    s.tail.on = on (params->tailOn);
    s.tail.keyHz = noteToHz (getTailNote());
    s.tail.start = idx (params->tailStartMode) == 1 ? attackSyncBeats (idx (params->tailStartSync)) * beatSeconds
                                                    : val (params->tailStart) * 0.001;
    s.startFrac = val (params->sampleStart) * 0.01;
    s.endFrac = jmax (s.startFrac, (double) val (params->sampleEnd) * 0.01);
    s.tail.glide = val (params->tailGlide) * 0.001;
    s.tail.level = Decibels::decibelsToGain (val (params->tailLevel));
    s.tail.tuneBody = on (params->tuneBody);
    s.velocitySensitivity = val (params->velSens) * 0.01f;
    return s;
}

int KickDrumAudioProcessor::keyLockedNote (int midiNote) const
{
    // the played note, snapped to the scale, in the octave nearest the Key knob
    const int knob = getTailNote();
    const int root = idx (params->tailRoot), scale = idx (params->tailScale);
    int note = snapToScale (midiNote, root, scale);
    while (note - knob > 6)  note -= 12;
    while (knob - note > 6)  note += 12;   // ties (a tritone away) go up
    return note;
}

int KickDrumAudioProcessor::getTailNote() const
{
    const int note = snapToScale (idx (params->tailKey), idx (params->tailRoot), idx (params->tailScale));
    return jlimit (kTailKeyMin - 1, kTailKeyMax + 1, note);
}

SaturationSettings KickDrumAudioProcessor::makeSaturationSettings() const
{
    SaturationSettings s;
    s.on = on (params->satOn);
    s.type = idx (params->satType);
    s.driveDb = val (params->satDrive);
    s.tone = val (params->satTone) * 0.01f;
    s.mix = val (params->satMix) * 0.01f;
    s.levelDb = val (params->satLevel);
    return s;
}

float KickDrumAudioProcessor::getVolumeGain() const
{
    const float db = val (params->volume);
    return db <= -59.9f ? 0.0f : Decibels::decibelsToGain (db);
}

CompressorSettings KickDrumAudioProcessor::makeCompressorSettings() const
{
    CompressorSettings c;
    c.on = on (params->compOn);
    c.thresholdDb = val (params->compThresh);
    c.ratio = val (params->compRatio);
    c.attackMs = val (params->compAttack);
    c.releaseMs = val (params->compRelease);
    c.makeupDb = val (params->compMakeup);
    c.mix = val (params->compMix) * 0.01f;
    return c;
}

//==============================================================================
void KickDrumAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();
    const int num = buffer.getNumSamples();

    // tempo: the host's, else the internal one
    {
        bool fromHost = false;
        float bpm = val (params->bpm);
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto hostBpm = pos->getBpm(); hostBpm.hasValue() && *hostBpm > 1.0)
                {
                    bpm = (float) *hostBpm;
                    fromHost = true;
                }
        tempo.store (bpm);
        tempoFromHost.store (fromHost);
    }

    const auto* set = bank.getAudioSet();
    const auto settings = makeEngineSettings();

    // a kick picked in a browser with Preview on plays as soon as it's ready
    if (set != nullptr && set->id != lastSetId)
    {
        lastSetId = set->id;
        if (set->preview)
            engine.trigger (set, settings, 1.0f);
    }
    if (const int mask = auditionRequest.exchange (-1); mask >= 0)
        engine.trigger (set, settings, 1.0f, (uint8) mask);

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = jlimit (0, num, meta.samplePosition);
        if (msg.isNoteOn())
        {
            engine.render (set, settings, buffer, pos, at - pos, bank.getPreviousSet());
            pos = at;
            if (on (params->keyLock))
            {
                // Key Lock: this note's pitch (in the scale) becomes the key of the tail,
                // and of the body too with Tune Body on
                const int keyNote = keyLockedNote (msg.getNoteNumber());
                lockedNote.store (keyNote);
                auto locked = settings;
                locked.tail.keyHz = noteToHz (keyNote);
                engine.trigger (set, locked, msg.getFloatVelocity());
            }
            else
            {
                engine.trigger (set, settings, msg.getFloatVelocity());
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            engine.render (set, settings, buffer, pos, at - pos, bank.getPreviousSet());
            pos = at;
            engine.reset();
        }
    }
    engine.render (set, settings, buffer, pos, num - pos, bank.getPreviousSet());
    if (! engine.isCrossfadingSets())
        bank.releasePrevious();     // the old band split is no longer needed
    midi.clear();

    fx.process (buffer, makeSaturationSettings(), makeCompressorSettings());

    const float volDb = val (params->volume);
    volumeSmooth.setTargetValue (volDb <= -59.9f ? 0.0f : Decibels::decibelsToGain (volDb));
    if (volumeSmooth.isSmoothing())
    {
        for (int i = 0; i < num; ++i)
        {
            const float g = volumeSmooth.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
        }
    }
    else
    {
        buffer.applyGain (volumeSmooth.getTargetValue());
    }

    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = jmax (peak, buffer.getMagnitude (ch, 0, num));
    outputPeak.store (jmax (outputPeak.load(), peak));

    // the editor's playhead lines up with what is heard (FX latency included)
    const double t = engine.getPlayheadSeconds();
    playhead.store (t < 0.0 ? -1.0 : jmax (0.0, t - getLatencySamples() / jmax (1.0, getSampleRate())));
}

//==============================================================================
void KickDrumAudioProcessor::timerCallback()
{
    kd::dsp::CrossoverSettings c;
    for (int i = 0; i < 3; ++i)
        c.freqs[(size_t) i] = val (params->xover[(size_t) i]);
    c.slopeIndex = idx (params->slope);
    bank.setCrossover (c);
}

void KickDrumAudioProcessor::loadGlobalFromUser (const File& file)
{
    bank.loadGlobal (file, browserStates[kGlobalBrowser].preview);
}

void KickDrumAudioProcessor::loadFromBrowser (int browserIndex, const File& file)
{
    const bool preview = getBrowserState (browserIndex).preview;
    if (browserIndex == kGlobalBrowser)
        bank.loadGlobal (file, preview);
    else
        bank.loadBand (browserIndex, file, preview);
}

//==============================================================================
int KickDrumAudioProcessor::getNumPrograms()        { return (int) getFactoryPresets().size(); }

const String KickDrumAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return isPositiveAndBelow (index, (int) presets.size()) ? String (presets[(size_t) index].name) : String();
}

void KickDrumAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram = index;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (rp->getParameterID() != ParamIDs::bpm)
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (auto& [id, value] : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

//==============================================================================
namespace
{
    const Identifier samplesTag { "Samples" }, globalPathId { "global" }, uiTag { "UI" },
                     browserTag { "Browser" }, programId { "program" };

    ValueTree browserToTree (const BrowserPanel::State& s, int index)
    {
        return ValueTree (browserTag, { { "index", index }, { "place", s.place }, { "autoLoad", s.autoLoad },
                                        { "preview", s.preview }, { "favOnly", s.favouritesOnly },
                                        { "tag", s.tag }, { "query", s.query }, { "folder", s.folder } });
    }

    void treeToBrowser (const ValueTree& t, BrowserPanel::State& s)
    {
        s.place = t.getProperty ("place", s.place);
        s.autoLoad = t.getProperty ("autoLoad", s.autoLoad);
        s.preview = t.getProperty ("preview", s.preview);
        s.favouritesOnly = t.getProperty ("favOnly", s.favouritesOnly);
        s.tag = t.getProperty ("tag", s.tag).toString();
        s.query = t.getProperty ("query", s.query).toString();
        s.folder = t.getProperty ("folder", s.folder).toString();
    }
}

void KickDrumAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (samplesTag), nullptr);
    state.removeChild (state.getChildWithName (uiTag), nullptr);

    ValueTree samples (samplesTag);
    samples.setProperty (globalPathId, bank.getGlobalPath(), nullptr);
    for (int b = 0; b < kNumBands; ++b)
        samples.setProperty ("band" + String (b + 1), bank.getBandPath (b), nullptr);
    state.appendChild (samples, nullptr);

    ValueTree ui (uiTag);
    ui.setProperty ("width", editorSize.x, nullptr);
    ui.setProperty ("height", editorSize.y, nullptr);
    ui.setProperty (programId, currentProgram, nullptr);
    ui.setProperty ("levelReference", selectedReference, nullptr);
    for (int i = 0; i <= kNumBands; ++i)
        ui.appendChild (browserToTree (browserStates[(size_t) i], i), nullptr);
    state.appendChild (ui, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KickDrumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = ValueTree::fromXml (*xml);
    const auto samples = state.getChildWithName (samplesTag);
    const auto ui = state.getChildWithName (uiTag);
    state.removeChild (samples, nullptr);
    state.removeChild (ui, nullptr);
    apvts.replaceState (state);

    // replaceState skips a parameter whose adapter already holds the restored value, but a bool
    // or choice parameter can hold an unsnapped raw value (e.g. 0.29 for "off"). Push every value
    // so the parameters exactly match the restored state.
    for (auto* p : getParameters())
    {
        if (auto* param = dynamic_cast<RangedAudioParameter*> (p))
        {
            const auto node = state.getChildWithProperty ("id", param->getParameterID());
            if (! node.isValid() || ! node.hasProperty ("value"))
                continue;

            const float target = param->convertTo0to1 ((float) node.getProperty ("value"));
            if (! approximatelyEqual (param->getValue(), target))
                param->setValueNotifyingHost (target);
        }
    }

    if (samples.isValid())
    {
        std::array<String, kNumBands> bandPaths;
        for (int b = 0; b < kNumBands; ++b)
            bandPaths[(size_t) b] = samples.getProperty ("band" + String (b + 1)).toString();
        bank.restore (samples.getProperty (globalPathId).toString(), bandPaths);
    }

    if (ui.isValid())
    {
        editorSize = { (int) ui.getProperty ("width", 0), (int) ui.getProperty ("height", 0) };
        currentProgram = jlimit (0, getNumPrograms() - 1, (int) ui.getProperty (programId, 0));
        selectedReference = ui.getProperty ("levelReference", String()).toString();
        for (const auto& child : ui)
            if (child.hasType (browserTag))
                treeToBrowser (child, browserStates[(size_t) jlimit (0, kNumBands, (int) child.getProperty ("index", 0))]);
    }

    timerCallback();
}

//==============================================================================
AudioProcessorEditor* KickDrumAudioProcessor::createEditor()
{
    return new KickDrumAudioProcessorEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickDrumAudioProcessor();
}
