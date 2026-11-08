#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/KickBank.h"
#include "../DSP/KickEngine.h"
#include "../DSP/OutputRenderer.h"

namespace kd::ui
{
    /** Accent colour of each band (crossover curves, band tags). */
    juce::Colour bandColour (int band);

    //==============================================================================
    /** Base for the blue LCD displays that accept dropped audio files. */
    class DropTarget : public juce::FileDragAndDropTarget
    {
    public:
        std::function<bool (const juce::File&)> isSupported;
        std::function<void (const juce::File&)> onFileDropped;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int, int) override;
        void fileDragEnter (const juce::StringArray&, int, int) override   { setDragHover (true); }
        void fileDragExit (const juce::StringArray&) override               { setDragHover (false); }

    protected:
        virtual void setDragHover (bool) = 0;
    };

    //==============================================================================
    /** The whole kick (the global sample), with its name, detected pitch and length. */
    class MainWaveform : public juce::Component,
                         public juce::SettableTooltipClient,
                         public DropTarget
    {
    public:
        MainWaveform();

        std::function<void()> onAudition, onBrowse;

        void setInfo (const KickBank::DisplayInfo&);
        void setPlayhead (double seconds, double ratio);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        void setDragHover (bool h) override   { dragHover = h; repaint(); }

        KickBank::DisplayInfo info;
        double playhead = -1.0, ratio = 1.0;
        bool dragHover = false;
    };

    //==============================================================================
    /**
        The four band gain curves on a log frequency axis. The three split points are
        handles: drag them sideways to move the crossovers (double-click resets).
    */
    class CrossoverDisplay : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        CrossoverDisplay (juce::RangedAudioParameter& x1, juce::RangedAudioParameter& x2,
                          juce::RangedAudioParameter& x3, juce::RangedAudioParameter& slope);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> plotArea() const;
        float xForFreq (double f) const;
        double freqForX (float x) const;
        int handleAt (float x) const;
        dsp::CrossoverSettings current() const;

        std::array<juce::RangedAudioParameter*, 3> params;
        std::array<std::unique_ptr<juce::ParameterAttachment>, 3> attachments;
        std::unique_ptr<juce::ParameterAttachment> slopeAttachment;
        std::array<float, 3> freqs { 120.0f, 1000.0f, 10000.0f };
        int slopeIndex = 3;
        int dragging = -1, hover = -1;
    };

    //==============================================================================
    /**
        One band: its waveform after the crossover, shaped by the Attack / Hold / Decay
        envelope (drawn on top), the tuned tail on the Sub band, and the playhead.
        Click to hear this band on its own, double-click to browse, drop a file to give
        the band its own sample.
    */
    class BandView : public juce::Component,
                     public juce::SettableTooltipClient,
                     public DropTarget
    {
    public:
        struct State
        {
            std::shared_ptr<const Overview> overview;
            BandSettings env;
            double spanSeconds = 1.0;       // shared by all four views
            double ratio = 1.0;             // Tune Body speed
            float displayScale = 1.0f;      // the loudest band fills the view
            bool tailOn = false;
            double tailStart = 0.12;
            float tailLevel = 1.0f;
            juce::String tailText;          // e.g. "TAIL A1"
            juce::String lengthText;        // e.g. "1/4  500 ms"
            juce::String rangeText;         // e.g. "30 - 120 Hz"
            bool enabled = true;

            bool operator== (const State&) const;
        };

        explicit BandView (int bandIndex);

        std::function<void()> onAudition, onBrowse;

        void setState (const State&);
        void setPlayhead (double seconds);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        void setDragHover (bool h) override   { dragHover = h; repaint(); }

        int band;
        State state;
        double playhead = -1.0;
        bool dragHover = false;
    };

    //==============================================================================
    /**
        The instrument's output for one hit (after every band, the tail, the FX and the
        volume), on a bar/beat grid at the current tempo. Drawn at the screen's physical
        resolution: every device pixel gets its own min/max from the full-rate audio.
    */
    class OutputView : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        OutputView();

        std::function<void()> onAudition;

        void setHit (std::shared_ptr<const RenderedHit>);
        void setTiming (double bpm, double viewBeats);
        void setPlayhead (double seconds);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        void rebuildPeaks (int columns);

        std::shared_ptr<const RenderedHit> hit;
        double bpm = 120.0, viewBeats = 4.0, playhead = -1.0;
        std::vector<float> peakMin, peakMax;   // one entry per physical pixel column
        int peaksFor = -1;
        const RenderedHit* peaksHit = nullptr;
        double peaksSpan = 0.0;
    };

    //==============================================================================
    /** A frequency you can type in (e.g. "120", "1.5k", "10 kHz"), bound to a parameter. */
    class FrequencyField : public juce::Label
    {
    public:
        explicit FrequencyField (juce::RangedAudioParameter&);

        void paint (juce::Graphics&) override;

    private:
        void textWasEdited() override;
        juce::TextEditor* createEditorComponent() override;

        juce::RangedAudioParameter& param;
        juce::ParameterAttachment attachment;
        float value = 0.0f;
    };

    //==============================================================================
    /** The saturation transfer curve (input level across, output up). */
    class SaturationCurve : public juce::Component
    {
    public:
        void setState (bool on, int type, float driveDb, float mix);
        void paint (juce::Graphics&) override;

    private:
        bool on = false;
        int type = 0;
        float driveDb = 0.0f, mix = 1.0f;
    };

    //==============================================================================
    /** Horizontal gain-reduction meter (0 .. 24 dB). */
    class GainReductionMeter : public juce::Component
    {
    public:
        void setValue (float dbReduction, bool active);
        void paint (juce::Graphics&) override;

    private:
        float value = 0.0f;
        bool active = false;
    };
}
