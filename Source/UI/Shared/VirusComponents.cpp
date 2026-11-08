#include "VirusComponents.h"
#include "CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
// VirusKnob
//==============================================================================
VirusKnob::VirusKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRange (0.0, 127.0, 0.0);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String VirusKnob::getTooltip()
{
    String text = label.toUpperCase() + "  " + getValueText();
    if (! isEnabled() && inactiveHint.isNotEmpty())
        text << "\n" << inactiveHint;
    else if (isEnabled())
        text << "\nDouble-click to type a value, Alt-click to reset";
    return text;
}

void VirusKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        knobArea = bounds;
    }
    else
    {
        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        const bool showValue = (hovering || dragging) && isEnabled();
        if (showValue)
        {
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (VirusColours::blue);
            g.drawFittedText (getValueText(), labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
        }
        else
        {
            CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
        }
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

String VirusKnob::getValueText() const
{
    // Shows real units (via describeValue) rather than Virus Delay's 0..127 values.
    const auto v = (float) getValue();
    return describeValue ? describeValue (v) : String (roundToInt (v));
}

void VirusKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void VirusKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        setValueWithGesture (defaultValue);
        return;
    }

    dragging = true;
    lastDragY = e.position.y;
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void VirusKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    // Incremental drag so pressing/releasing shift mid-drag never jumps.
    const float pixelsForFullRange = e.mods.isShiftDown() ? 1500.0f : 200.0f;
    const double delta = (double) (lastDragY - e.position.y) * (getMaximum() - getMinimum()) / pixelsForFullRange;
    lastDragY = e.position.y;

    if (std::abs (delta) > 1.0e-9)
        setValue (jlimit (getMinimum(), getMaximum(), getValue() + delta), sendNotificationSync);
}

void VirusKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void VirusKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        showValueEditor();
}

void VirusKnob::showValueEditor()
{
    auto* parent = getParentComponent();
    if (parent == nullptr || valueEditor != nullptr)
        return;

    valueEditor = std::make_unique<TextEditor>();
    auto& ed = *valueEditor;
    ed.setFont (CustomLookAndFeel::uiFont (12.5f, true));
    ed.setJustification (Justification::centred);
    ed.setColour (TextEditor::backgroundColourId, VirusColours::lcdBottom);
    ed.setColour (TextEditor::textColourId, Colours::white);
    ed.setColour (TextEditor::outlineColourId, VirusColours::blue);
    ed.setColour (TextEditor::focusedOutlineColourId, Colours::white);
    ed.setColour (TextEditor::highlightColourId, VirusColours::blueGlow.withAlpha (0.7f));
    ed.setColour (CaretComponent::caretColourId, Colours::white);
    ed.setIndents (2, 3);
    ed.setText (getValueText().upToFirstOccurrenceOf ("  (", false, false), false);
    ed.selectAll();

    const int w = jmax (getWidth(), 76), h = 22;
    ed.setBounds (Rectangle<int> (w, h).withCentre (getBounds().getCentre()).constrainedWithin (parent->getLocalBounds()));
    ed.onReturnKey = [this] { closeValueEditor (true); };
    ed.onEscapeKey = [this] { closeValueEditor (false); };
    ed.onFocusLost = [this] { closeValueEditor (true); };

    parent->addAndMakeVisible (ed);
    ed.grabKeyboardFocus();
}

void VirusKnob::closeValueEditor (bool apply)
{
    if (valueEditor == nullptr)
        return;

    // take it out first: removing the editor makes it lose focus, which calls back in here
    auto ed = std::move (valueEditor);
    const auto text = ed->getText().trim();
    ed->onFocusLost = nullptr;
    if (auto* parent = ed->getParentComponent())
        parent->removeChildComponent (ed.get());

    if (apply && text.isNotEmpty())
    {
        double v = std::numeric_limits<double>::quiet_NaN();
        if (parseValue)
            v = parseValue (text);
        else if (text.containsAnyOf ("0123456789"))
            v = text.retainCharacters ("-+0123456789.,").replaceCharacter (',', '.').getDoubleValue();

        if (std::isfinite (v))
            setValueWithGesture (v);
    }
    repaint();

    // the editor may still be inside its own callback: delete it afterwards
    MessageManager::callAsync ([e = std::shared_ptr<TextEditor> (std::move (ed))] {});
}

void VirusKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    const double step = e.mods.isShiftDown() ? 1.0 : 4.0;
    const double amount = jlimit (-1.0, 1.0, (double) dir * 8.0) * step;
    setValueWithGesture (getValue() + amount);
}

void VirusKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void VirusKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, VirusKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto range = p.getNormalisableRange();
    knob.setRange (range.start, range.end, 0.0);
    knob.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, dimmed);

    const auto textColour = dimmed ? Colour (0xff8a9097) : Colours::white;
    auto textArea = b.reduced (6.0f, 0.0f);

    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawText (nameText, textArea.translated (0.0f, 1.0f), Justification::centredLeft);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), Justification::centredRight);
    g.setColour (textColour);
    g.drawText (nameText, textArea, Justification::centredLeft);
    g.drawText (valueText, textArea, Justification::centredRight);
}

//==============================================================================
// TempoField
//==============================================================================
TempoField::TempoField (RangedAudioParameter& p)
    : param (p),
      attachment (p, [this] (float v) { internalBpm = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::UpDownResizeCursor);
}

void TempoField::setTempo (float bpm, bool fromHost)
{
    if (std::abs (bpm - shownBpm) > 0.01f || fromHost != hostTempo)
    {
        shownBpm = bpm;
        hostTempo = fromHost;
        setTooltip (fromHost ? "Tempo is provided by the host"
                             : "Internal tempo (no host tempo). Drag, shift-drag for fine, double-click = default");
        setMouseCursor (fromHost ? MouseCursor::NormalCursor : MouseCursor::UpDownResizeCursor);
        repaint();
    }
}

void TempoField::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b);

    auto tag = b.removeFromLeft (38.0f).reduced (3.0f);
    g.setColour (hostTempo ? Colour (0xff2a6fb8) : Colour (0xff4b5056));
    g.fillRoundedRectangle (tag, 2.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    g.drawText (hostTempo ? "HOST" : "INT", tag, Justification::centred);

    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText (String (hostTempo ? shownBpm : internalBpm, 1) + " BPM", b.reduced (4.0f, 0.0f), Justification::centredRight);
}

void TempoField::mouseDown (const MouseEvent& e)
{
    if (hostTempo)
        return;
    dragging = true;
    lastY = e.position.y;
    attachment.beginGesture();
}

void TempoField::mouseDrag (const MouseEvent& e)
{
    if (! dragging)
        return;
    const float perPixel = e.mods.isShiftDown() ? 0.1f : 0.5f;
    const float delta = (lastY - e.position.y) * perPixel;
    lastY = e.position.y;
    const auto range = param.getNormalisableRange();
    attachment.setValueAsPartOfGesture (jlimit (range.start, range.end, internalBpm + delta));
}

void TempoField::mouseUp (const MouseEvent&)
{
    if (dragging)
        attachment.endGesture();
    dragging = false;
}

void TempoField::mouseDoubleClick (const MouseEvent&)
{
    if (! hostTempo)
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

//==============================================================================
// HelpButton / HelpOverlay
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (VirusColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}
