/*
  ==============================================================================

    Redoondant: finds files and folders that are probably no longer needed
    and moves the chosen ones to the Recycle Bin.

    Run with --test to execute the unit tests and exit.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class RedoondantApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--test"))
        {
            runTests();
            return;
        }

        settings = std::make_unique<Settings>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        mainWindow = nullptr;

        if (settings != nullptr)
            settings->flush();

        settings = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    //==============================================================================
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, Settings& s)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons),
              settings (s.root())
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (s), true);
            setResizable (true, true);
            setResizeLimits (900, 500, 10000, 10000);

            if (! restoreWindowStateFromString (settings.get ("window")))
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
        }

        ~MainWindow() override
        {
            settings.set ("window", getWindowStateAsString());
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        SettingsScope settings;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<Settings> settings;
    std::unique_ptr<MainWindow> mainWindow;

    /** Runs all juce::UnitTests, logging to %TEMP%\Redoondant-tests.log; the exit code is the failure count. */
    void runTests()
    {
        const auto logFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Redoondant-tests.log");
        logFile.deleteFile();
        juce::FileLogger logger (logFile, "Redoondant unit tests");
        juce::Logger::setCurrentLogger (&logger);

        juce::UnitTestRunner runner;
        runner.runAllTests();

        int failures = 0;

        for (int i = 0; i < runner.getNumResults(); ++i)
            failures += runner.getResult (i)->failures;

        juce::Logger::writeToLog (failures == 0 ? "ALL TESTS PASSED" : "FAILURES: " + juce::String (failures));
        juce::Logger::setCurrentLogger (nullptr);

        setApplicationReturnValue (failures);
        quit();
    }
};

//==============================================================================
START_JUCE_APPLICATION (RedoondantApplication)
