#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    "Drag Audio Out" grip. Dragging it writes the rendered hit to a WAV file and starts
    an external (OS-level) file drag, so it can be dropped on a DAW track (as ArpSampler's
    Drag MIDI). Right-click offers "Save WAV file..." for hosts that don't accept drags.
*/
class DragAudioHandle : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    DragAudioHandle();

    /** Writes the audio and returns the file (empty on failure). */
    std::function<juce::File()> createFile;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override   { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void showMenu();

    bool hover = false, dragging = false, dragStarted = false;
    std::unique_ptr<juce::FileChooser> chooser;
};
