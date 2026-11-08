#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Rotary knob with Virus-style interaction:
      - vertical drag moves in whole steps (1.0); hold Shift for 0.1 steps
      - scroll wheel: one notch = 1.0, or 0.1 with Shift
      - double-click to type a value (Enter applies, Esc cancels)
      - Alt-click or Ctrl/Cmd-click resets to the default value
      - small knobs show their label underneath, which swaps to the value
        (in real units, via describeValue) while hovering or dragging
      - tooltips show real units
    Drawing is delegated to CustomLookAndFeel::drawRotarySlider.
*/
class VirusKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    VirusKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (float)> describeValue;   // real units for the label and tooltip
    /** Turns typed text into a value ("120 ms", "1.5 s", "-3 dB" ...). Returns NaN if it can't.
        Without it, the first number in the text is used. */
    std::function<double (const juce::String&)> parseValue;
    juce::String inactiveHint;                           // shown in the tooltip while disabled

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String getValueText() const;

    /** Opens the type-in field (double-click does this). */
    void showValueEditor();

private:
    void setValueWithGesture (double v);
    void closeValueEditor (bool apply);
    std::unique_ptr<juce::TextEditor> valueEditor;

    Size knobSize;
    juce::String label;
    double defaultValue = 64.0;
    float lastDragY = 0.0f;
    double dragValue = 0.0;          // un-snapped drag position
    bool dragShift = false;
    bool dragging = false, hovering = false;
};

//==============================================================================
/** Connects a VirusKnob to a parameter with proper host gesture handling. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, VirusKnob&);
    ~KnobAttachment();

private:
    VirusKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "TIME      96". */
class LcdDisplay : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setDimmed (bool shouldDim);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
    bool dimmed = false;
};

//==============================================================================
/** Tempo readout. Shows the host tempo, or the draggable internal fallback BPM. */
class TempoField : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit TempoField (juce::RangedAudioParameter& bpmParam);

    void setTempo (float bpm, bool fromHost);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float internalBpm = 120.0f, shownBpm = 120.0f, lastY = 0.0f;
    bool hostTempo = false, dragging = false;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};
