#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace kd
{
    static constexpr int kNumBands = 4;

    //==============================================================================
    namespace ParamIDs
    {
        // Crossover
        inline constexpr const char* xover1 = "xover1";
        inline constexpr const char* xover2 = "xover2";
        inline constexpr const char* xover3 = "xover3";
        inline constexpr const char* slope  = "slope";

        // Tail
        inline constexpr const char* tailOn    = "tailOn";
        inline constexpr const char* tailKey   = "tailKey";
        inline constexpr const char* tailRoot  = "tailRoot";
        inline constexpr const char* tailScale = "tailScale";
        inline constexpr const char* tailStart = "tailStart";
        inline constexpr const char* tailGlide = "tailGlide";
        inline constexpr const char* tailLevel = "tailLevel";
        inline constexpr const char* tuneBody  = "tuneBody";
        inline constexpr const char* keyLock   = "keyLock";    // MIDI notes set the key (scale-locked)

        // Saturation
        inline constexpr const char* satOn    = "satOn";
        inline constexpr const char* satType  = "satType";
        inline constexpr const char* satDrive = "satDrive";
        inline constexpr const char* satTone  = "satTone";
        inline constexpr const char* satMix   = "satMix";
        inline constexpr const char* satLevel = "satLevel";

        // Compressor
        inline constexpr const char* compOn      = "compOn";
        inline constexpr const char* compThresh  = "compThresh";
        inline constexpr const char* compRatio   = "compRatio";
        inline constexpr const char* compAttack  = "compAttack";
        inline constexpr const char* compRelease = "compRelease";
        inline constexpr const char* compMakeup  = "compMakeup";
        inline constexpr const char* compMix     = "compMix";

        // Master
        inline constexpr const char* velSens = "velSens";
        inline constexpr const char* volume  = "volume";
        inline constexpr const char* bpm     = "bpm";
    }

    /** Per-band parameter IDs, e.g. band (0, "Attack") -> "b1Attack". */
    inline juce::String bandParam (int band, const char* name)   { return "b" + juce::String (band + 1) + name; }

    namespace BandParams
    {
        inline constexpr const char* attack    = "Attack";
        inline constexpr const char* attackMode = "AttackMode"; // 0 = ms, 1 = sync
        inline constexpr const char* attackSync = "AttackSync";
        inline constexpr const char* length    = "Length";   // whole band: attack + sustain + decay
        inline constexpr const char* decay     = "Decay";
        inline constexpr const char* curve     = "Curve";
        inline constexpr const char* level     = "Level";
        inline constexpr const char* decayMode = "DecayMode";   // Length in 0 = ms, 1 = sync
        inline constexpr const char* sync      = "Sync";        // synced Length
        inline constexpr const char* mute      = "Mute";
        inline constexpr const char* solo      = "Solo";
    }

    //==============================================================================
    inline juce::StringArray getSlopeNames()      { return { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct" }; }
    inline juce::StringArray getSyncNames()       { return { "1/16", "1/8", "1/4", "1/2", "1 Bar" }; }
    /** Length of each sync choice in quarter notes. */
    inline double syncBeats (int index)
    {
        static constexpr double beats[] { 0.25, 0.5, 1.0, 2.0, 4.0 };
        return beats[juce::jlimit (0, 4, index)];
    }
    /** The output display always shows one quarter note. */
    static constexpr double kOutputViewBeats = 1.0;

    inline juce::StringArray getAttackSyncNames() { return { "1/64", "1/32", "1/16", "1/8", "1/4" }; }
    /** Length of each attack sync choice in quarter notes. */
    inline double attackSyncBeats (int index)
    {
        static constexpr double beats[] { 0.0625, 0.125, 0.25, 0.5, 1.0 };
        return beats[juce::jlimit (0, 4, index)];
    }
    inline juce::StringArray getBandNames()       { return { "Sub", "Body", "Click", "Air" }; }
    inline juce::StringArray getSatTypeNames()    { return { "Soft", "Tape", "Tube", "Hard Clip", "Foldback", "Fuzz" }; }
    inline juce::StringArray getKeyNames()        { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    inline juce::StringArray getScaleNames()
    {
        return { "Chromatic", "Major", "Minor", "Harmonic Minor", "Dorian", "Phrygian",
                 "Mixolydian", "Major Pentatonic", "Minor Pentatonic", "Phrygian Dominant" };
    }

    /** Bit n set = pitch class n (semitones above the root) is in the scale. */
    inline std::uint16_t scaleMask (int scaleIndex)
    {
        static constexpr std::array<std::uint16_t, 10> masks
        {
            0x0fff,                                                       // Chromatic
            (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   // Major
            (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // Minor
            (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   // Harmonic Minor
            (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   // Dorian
            (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // Phrygian
            (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   // Mixolydian
            (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                           // Major Pentatonic
            (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                          // Minor Pentatonic
            (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // Phrygian Dominant
        };
        return masks[(size_t) juce::jlimit (0, (int) masks.size() - 1, scaleIndex)];
    }

    /** Nearest note of the scale (ties go down). */
    inline int snapToScale (int note, int root, int scaleIndex)
    {
        const auto mask = scaleMask (scaleIndex);
        auto inScale = [&] (int n) { return ((mask >> (((n - root) % 12 + 12) % 12)) & 1u) != 0; };
        for (int d = 0; d <= 6; ++d)
        {
            if (inScale (note - d)) return note - d;
            if (inScale (note + d)) return note + d;
        }
        return note;
    }

    inline juce::String noteName (int note)
    {
        return getKeyNames()[((note % 12) + 12) % 12] + juce::String (note / 12 - 1);
    }

    inline double noteToHz (double note)  { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
    inline double hzToNote (double hz)    { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

    static constexpr int kTailKeyMin = 24, kTailKeyMax = 59;   // C1 .. B3

    //==============================================================================
    inline juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, interval);
        r.setSkewForCentre (centre);
        return r;
    }

    inline juce::String formatMs (float ms)
    {
        if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 2) + " s";
        if (ms >= 100.0f)  return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms, 1) + " ms";
    }

    inline juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f) return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    inline juce::String formatDb (float db)
    {
        if (db <= -59.9f) return "-inf dB";
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    inline juce::String formatPercent (float v)    { return juce::String (juce::roundToInt (v)) + " %"; }

    /** Decay curve: negative = holds then falls (slow start), 0 = linear, positive = exponential. */
    inline juce::String formatCurve (float v)
    {
        const int n = juce::roundToInt (v);
        if (n == 0) return "Linear";
        return (n > 0 ? "Exp " : "Slow ") + juce::String (std::abs (n));
    }

    //==============================================================================
    /** What a typed value means, for knobs you can type into. */
    enum class Unit { Plain, Ms, Hz, Db, Percent, Ratio, Curve, Note };

    /** The first number in the text (with sign and decimal point), or NaN if there's none. */
    inline double firstNumber (const juce::String& text)
    {
        auto t = text.replaceCharacter (',', '.');
        int start = 0;
        while (start < t.length() && ! juce::CharacterFunctions::isDigit (t[start]) && t[start] != '.')
            ++start;
        if (start >= t.length())
            return std::numeric_limits<double>::quiet_NaN();
        const bool negative = start > 0 && t[start - 1] == '-';
        const auto number = t.substring (start).initialSectionContainingOnly ("0123456789.").getDoubleValue();
        return negative ? -number : number;
    }

    /** Parses what was typed into a knob: "120", "120 ms", "1.5 s", "2k", "-3 dB", "-inf", "40 %",
        "4:1", "Exp 40", "Slow 30", "Linear", "A1", "F#2", "Bb1". Returns NaN if it can't. */
    inline double parseValueText (const juce::String& typed, Unit unit)
    {
        const auto text = typed.trim().toLowerCase();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double n = firstNumber (text);

        switch (unit)
        {
            case Unit::Ms:
                if (std::isnan (n)) return nan;
                // "ms" (or no unit) = milliseconds, "s" / "sec" = seconds
                return (text.contains ("ms") || ! text.containsChar ('s')) ? n : n * 1000.0;

            case Unit::Hz:
                if (std::isnan (n)) return nan;
                return text.containsChar ('k') ? n * 1000.0 : n;

            case Unit::Db:
                if (text.contains ("inf")) return -60.0;
                return n;

            case Unit::Curve:
                if (text.startsWith ("lin")) return 0.0;
                if (std::isnan (n)) return nan;
                if (text.startsWith ("slow") || text.startsWith ("log")) return -std::abs (n);
                if (text.startsWith ("exp")) return std::abs (n);
                return n;

            case Unit::Note:
            {
                const auto t = text.removeCharacters (" ");
                if (t.isEmpty()) return nan;
                const int letter = juce::String ("c d ef g a b").indexOfChar (t[0]);   // semitone of each letter
                if (letter < 0 || t[0] == ' ')
                    return n;                                  // a MIDI note number
                int note = letter, pos = 1;
                if (t[pos] == '#') { ++note; ++pos; }
                else if (t[pos] == 'b' && t.length() > pos + 1) { --note; ++pos; }
                const auto octaveText = t.substring (pos);
                if (! octaveText.containsAnyOf ("0123456789")) return nan;
                const int octave = octaveText.getIntValue();
                return (octave + 1) * 12 + note;
            }

            case Unit::Ratio:
            case Unit::Percent:
            case Unit::Plain:
            default:
                return n;
        }
    }

    //==============================================================================
    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        int version = 1;

        auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                             std::function<String (float)> toText, const String& label = {})
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id, version }, name, range, def,
                AudioParameterFloatAttributes().withLabel (label)
                                               .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })));
        };
        auto addChoice = [&] (const String& id, const String& name, const StringArray& choices, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, choices, def));
        };
        auto addBool = [&] (const String& id, const String& name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, version }, name, def));
        };

        // Crossover ----------------------------------------------------------------
        addFloat (ParamIDs::xover1, "Crossover 1", skewedRange (30.0f, 500.0f, 120.0f), 120.0f, formatHz, "Hz");
        addFloat (ParamIDs::xover2, "Crossover 2", skewedRange (200.0f, 5000.0f, 1000.0f), 1000.0f, formatHz, "Hz");
        addFloat (ParamIDs::xover3, "Crossover 3", skewedRange (2000.0f, 18000.0f, 10000.0f), 10000.0f, formatHz, "Hz");
        addChoice (ParamIDs::slope, "Crossover Slope", getSlopeNames(), 3);

        // Bands --------------------------------------------------------------------
        const auto bandNames = getBandNames();
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto prefix = bandNames[b] + " ";
            addFloat (bandParam (b, BandParams::attack), prefix + "Attack", skewedRange (0.0f, 500.0f, 20.0f), 0.0f, formatMs, "ms");
            addFloat (bandParam (b, BandParams::length), prefix + "Length", skewedRange (10.0f, 8000.0f, 500.0f), 900.0f, formatMs, "ms");
            addFloat (bandParam (b, BandParams::decay),  prefix + "Decay",  skewedRange (5.0f, 8000.0f, 400.0f), 600.0f, formatMs, "ms");
            addFloat (bandParam (b, BandParams::curve),  prefix + "Curve",  { -100.0f, 100.0f }, 40.0f, formatCurve);
            addFloat (bandParam (b, BandParams::level),  prefix + "Level",  { -60.0f, 12.0f }, 0.0f, formatDb, "dB");
            addChoice (bandParam (b, BandParams::attackMode), prefix + "Attack Mode", { "ms", "Sync" }, 0);
            addChoice (bandParam (b, BandParams::attackSync), prefix + "Attack Sync", getAttackSyncNames(), 1);
            addChoice (bandParam (b, BandParams::decayMode), prefix + "Length Mode", { "ms", "Sync" }, 0);
            addChoice (bandParam (b, BandParams::sync),  prefix + "Length Sync", getSyncNames(), 2);
            addBool (bandParam (b, BandParams::mute), prefix + "Mute", false);
            addBool (bandParam (b, BandParams::solo), prefix + "Solo", false);
        }

        // Tail ---------------------------------------------------------------------
        addBool (ParamIDs::tailOn, "Tail On", false);
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::tailKey, version }, "Tail Key",
                                                         kTailKeyMin, kTailKeyMax, 33,
                                                         AudioParameterIntAttributes().withStringFromValueFunction (
                                                             [] (int v, int) { return noteName (v); })));
        addChoice (ParamIDs::tailRoot, "Scale Root", getKeyNames(), 9);
        addChoice (ParamIDs::tailScale, "Scale", getScaleNames(), 2);
        addFloat (ParamIDs::tailStart, "Tail Start", skewedRange (20.0f, 600.0f, 150.0f), 120.0f, formatMs, "ms");
        addFloat (ParamIDs::tailGlide, "Tail Glide", skewedRange (0.0f, 1000.0f, 120.0f), 60.0f, formatMs, "ms");
        addFloat (ParamIDs::tailLevel, "Tail Level", { -24.0f, 12.0f }, 0.0f, formatDb, "dB");
        addBool (ParamIDs::tuneBody, "Tune Body", false);
        addBool (ParamIDs::keyLock, "Key Lock", false);

        // Saturation -----------------------------------------------------------------
        addBool (ParamIDs::satOn, "Saturation On", false);
        addChoice (ParamIDs::satType, "Saturation Type", getSatTypeNames(), 0);
        addFloat (ParamIDs::satDrive, "Drive", { 0.0f, 36.0f }, 6.0f, formatDb, "dB");
        addFloat (ParamIDs::satTone, "Tone", { 0.0f, 100.0f }, 100.0f, formatPercent, "%");
        addFloat (ParamIDs::satMix, "Saturation Mix", { 0.0f, 100.0f }, 100.0f, formatPercent, "%");
        addFloat (ParamIDs::satLevel, "Saturation Level", { -24.0f, 6.0f }, 0.0f, formatDb, "dB");

        // Compressor -------------------------------------------------------------------
        addBool (ParamIDs::compOn, "Compressor On", false);
        addFloat (ParamIDs::compThresh, "Threshold", { -60.0f, 0.0f }, -12.0f, formatDb, "dB");
        addFloat (ParamIDs::compRatio, "Ratio", skewedRange (1.0f, 20.0f, 4.0f), 4.0f,
                  [] (float v) { return String (v, v < 10.0f ? 1 : 0) + ":1"; });
        addFloat (ParamIDs::compAttack, "Comp Attack", skewedRange (0.1f, 100.0f, 10.0f), 10.0f, formatMs, "ms");
        addFloat (ParamIDs::compRelease, "Comp Release", skewedRange (5.0f, 1000.0f, 120.0f), 120.0f, formatMs, "ms");
        addFloat (ParamIDs::compMakeup, "Makeup", { 0.0f, 24.0f }, 0.0f, formatDb, "dB");
        addFloat (ParamIDs::compMix, "Comp Mix", { 0.0f, 100.0f }, 100.0f, formatPercent, "%");

        // Master -------------------------------------------------------------------------
        addFloat (ParamIDs::velSens, "Velocity Sensitivity", { 0.0f, 100.0f }, 50.0f, formatPercent, "%");
        addFloat (ParamIDs::volume, "Volume", { -60.0f, 12.0f }, 0.0f, formatDb, "dB");
        addFloat (ParamIDs::bpm, "Internal Tempo", { 40.0f, 240.0f }, 120.0f,
                  [] (float v) { return String (v, 1) + " BPM"; }, "BPM");

        return layout;
    }
}
