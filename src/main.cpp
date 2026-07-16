#include <JuceHeader.h>

#include "MainComponent.h"

class PracticeTakesApplication final : public juce::JUCEApplication
{
public:
    [[nodiscard]] const juce::String getApplicationName() override
    {
        return "Practice Takes";
    }

    [[nodiscard]] const juce::String getApplicationVersion() override
    {
        return "0.1.0";
    }

    [[nodiscard]] bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(
                name,
                juce::Colour::fromRGB(18, 20, 27),
                DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PracticeTakesApplication)
