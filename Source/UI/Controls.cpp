#include "Controls.h"
#include "Shared/CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
SegmentedButtons::SegmentedButtons (RangedAudioParameter& p, const StringArray& labels, const String& tooltip)
    : param (p),
      attachment (p, [this] (float v) { select (roundToInt (v) - minimum); })
{
    static int nextGroup = 9000;
    const int group = ++nextGroup;
    minimum = roundToInt (p.getNormalisableRange().start);

    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new TextButton (labels[i]));
        b->setRadioGroupId (group, dontSendNotification);
        b->setClickingTogglesState (true);
        b->setTooltip (tooltip);
        b->setConnectedEdges ((i > 0 ? Button::ConnectedOnLeft : 0) | (i < labels.size() - 1 ? Button::ConnectedOnRight : 0));
        b->onClick = [this, i]
        {
            if (buttons[i]->getToggleState())
                attachment.setValueAsCompleteGesture ((float) (minimum + i));
        };
        addAndMakeVisible (b);
    }

    attachment.sendInitialUpdate();
}

void SegmentedButtons::select (int index)
{
    if (isPositiveAndBelow (index, buttons.size()))
        buttons[index]->setToggleState (true, dontSendNotification);
}

void SegmentedButtons::resized()
{
    const int n = buttons.size();
    const float w = (float) getWidth() / (float) jmax (1, n);
    for (int i = 0; i < n; ++i)
        buttons[i]->setBounds (roundToInt (i * w), 0, roundToInt ((i + 1) * w) - roundToInt (i * w) + (i < n - 1 ? 1 : 0), getHeight());
}

//==============================================================================
void ArrowIconButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (TextButton::buttonColourId), isHighlighted, isDown);

    auto b = getLocalBounds().toFloat().reduced (getWidth() * 0.32f, getHeight() * 0.3f);
    Path arrow;
    if (right)
        arrow.addTriangle (b.getX(), b.getY(), b.getRight(), b.getCentreY(), b.getX(), b.getBottom());
    else
        arrow.addTriangle (b.getRight(), b.getY(), b.getX(), b.getCentreY(), b.getRight(), b.getBottom());

    g.setColour (Colour (0xffdfe3e6).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
    g.fillPath (arrow);
}
