#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "PluginProcessor.h"

//==============================================================================
// Renders the editor to a PNG without a display:
//   KickDrumTests --snapshot out.png [width] [preset] [browser index]
static int writeSnapshot (const juce::File& file, int width, int preset, int browser)
{
    KickDrumAudioProcessor processor;
    if (preset > 0)
        processor.setCurrentProgram (preset);
    processor.setOpenBrowser (browser);
    processor.getBank().waitUntilIdle (5000);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // deliver change messages

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (width > 0)
        editor->setSize (width, juce::roundToInt (width * (double) editor->getHeight() / editor->getWidth()));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    if (! out.openedOk() || ! png.writeImageToStream (image, out))
        return 1;

    std::cout << "Wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
    return 0;
}

//==============================================================================
// Runs every juce::UnitTest registered in this executable.  Usage: KickDrumTests [category]
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return writeSnapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                              argc > 3 ? juce::String (argv[3]).getIntValue() : 0,
                              argc > 4 ? juce::String (argv[4]).getIntValue() : 0,
                              argc > 5 ? juce::String (argv[5]).getIntValue() : -1);

    // print to stdout (the default goes to the debugger output on Windows, which CI can't see)
    struct ConsoleRunner : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override  { std::cout << message << std::endl; }
    };
    ConsoleRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
        runner.runTestsInCategory (argv[1]);
    else
        runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return failures > 0 ? 1 : 0;
}
