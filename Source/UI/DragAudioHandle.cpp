#include "DragAudioHandle.h"
#include "Shared/CustomLookAndFeel.h"

using namespace juce;

DragAudioHandle::DragAudioHandle()
{
    setMouseCursor (MouseCursor::DraggingHandCursor);
    setTooltip ("Drag onto a track in your DAW to drop the kick as audio (a WAV of one hit, exactly as the "
                "output display shows it, starting on the beat).\nRight-click to save the WAV file instead.");
}

void DragAudioHandle::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool lit = hover || dragging;

    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 3.0f);
    g.setGradientFill (ColourGradient (lit ? Colour (0xff3a95f0) : Colour (0xff2f86e0), 0.0f, b.getY(),
                                       lit ? Colour (0xff1d5fa6) : Colour (0xff15508f), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + 3.0f, b.getRight() - 3.0f);
    g.setColour (VirusColours::bevelDark);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    // grip dots
    auto grip = b.removeFromLeft (22.0f).reduced (7.0f, b.getHeight() * 0.22f);
    g.setColour (Colours::white.withAlpha (0.75f));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse (grip.getX() + col * 5.0f, grip.getY() + row * grip.getHeight() / 2.5f, 2.6f, 2.6f);

    // tiny kick waveform icon
    auto icon = b.removeFromRight (26.0f).reduced (5.0f, b.getHeight() * 0.22f);
    Path wave;
    const int n = 24;
    for (int i = 0; i <= n; ++i)
    {
        const float x = (float) i / (float) n;
        const float y = std::sin (x * 14.0f) * std::exp (-x * 3.5f);
        const float px = icon.getX() + x * icon.getWidth(), py = icon.getCentreY() - y * icon.getHeight() * 0.5f;
        if (i == 0) wave.startNewSubPath (px, py);
        else        wave.lineTo (px, py);
    }
    g.strokePath (wave, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    g.setColour (Colours::white);
    g.drawText ("Drag Audio Out", b, Justification::centred);
}

void DragAudioHandle::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    dragging = true;
    dragStarted = false;
    repaint();
}

void DragAudioHandle::mouseDrag (const MouseEvent& e)
{
    if (! dragging || dragStarted || e.getDistanceFromDragStart() < 4 || ! createFile)
        return;

    dragStarted = true;
    const auto file = createFile();
    if (file.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                              [safe = SafePointer<DragAudioHandle> (this)]
                                                              {
                                                                  if (safe != nullptr)
                                                                  {
                                                                      safe->dragging = false;
                                                                      safe->repaint();
                                                                  }
                                                              });
}

void DragAudioHandle::mouseUp (const MouseEvent&)
{
    dragging = false;
    repaint();
}

void DragAudioHandle::showMenu()
{
    PopupMenu menu;
    menu.addItem ("Save WAV file...", [this]
    {
        if (! createFile)
            return;
        const auto source = createFile();
        if (! source.existsAsFile())
            return;

        chooser = std::make_unique<FileChooser> ("Save the kick as a WAV file",
                                                 File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (source.getFileName()),
                                                 "*.wav");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                              [source] (const FileChooser& fc)
                              {
                                  auto target = fc.getResult();
                                  if (target != File())
                                      source.copyFileTo (target.withFileExtension ("wav"));
                              });
    });
    menu.addItem ("Show export folder", [this]
    {
        if (createFile)
        {
            const auto file = createFile();
            if (file.existsAsFile())
                file.revealToUser();
        }
    });
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}
