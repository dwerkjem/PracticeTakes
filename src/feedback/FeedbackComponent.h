#pragma once

#include <JuceHeader.h>

class FeedbackComponent final : public juce::Component, private juce::Thread
{
  public:
    enum class SubmissionState
    {
        editing,
        submitting,
        succeeded,
        queued,
        failed
    };

    explicit FeedbackComponent(
        juce::PropertiesFile& propertiesFile,
        const juce::String& initialContext = {});
    ~FeedbackComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setContextTag(const juce::String& toolOrWorkflowName);

  private:
    struct Draft
    {
        int type = 1;
        juce::String title;
        juce::String description;
        juce::String reproductionSteps;
        juce::String contactEmail;
        juce::String contextTag;
        bool includeVersion = false;
        bool includeOperatingSystem = false;
        bool includeScreenshot = false;
        juce::String screenshotBase64;
        juce::String clientSubmissionId;
    };

    void configureEditor(
        juce::TextEditor& editor,
        const juce::String& accessibleName,
        int focusOrder,
        bool multiline);
    void preview();
    void submit();
    [[nodiscard]] juce::String validate() const;
    [[nodiscard]] Draft currentDraft() const;
    [[nodiscard]] juce::String previewText(const Draft& draft) const;
    [[nodiscard]] juce::String captureApplicationScreenshot() const;
    void saveDraft(const Draft& draft);
    void clearDraft();
    void restoreDraft();
    void setSubmissionState(SubmissionState state, const juce::String& detail = {});
    void run() override;

    juce::PropertiesFile& properties;
    juce::ComboBox typeBox;
    juce::Label typeLabel;
    juce::Label titleLabel;
    juce::TextEditor titleEditor;
    juce::Label descriptionLabel;
    juce::TextEditor descriptionEditor;
    juce::Label reproductionLabel;
    juce::TextEditor reproductionEditor;
    juce::Label emailLabel;
    juce::TextEditor emailEditor;
    juce::Label contextLabel;
    juce::TextEditor contextEditor;
    juce::Label environmentLabel;
    juce::ToggleButton versionDiagnostic{"Application version"};
    juce::ToggleButton operatingSystemDiagnostic{"Operating system"};
    juce::ToggleButton screenshotDiagnostic{"Application screenshot"};
    juce::Label validationLabel;
    juce::TextButton previewButton{"Preview"};
    juce::TextButton submitButton{"Submit feedback"};

    Draft pendingDraft;
    SubmissionState submissionState = SubmissionState::editing;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackComponent)
};
