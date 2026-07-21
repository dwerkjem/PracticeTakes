#include "FeedbackComponent.h"

namespace
{
constexpr int maximumTitleLength = 120;
constexpr int maximumDescriptionLength = 6500;
constexpr int maximumReproductionLength = 1200;
constexpr int maximumEmailLength = 254;

constexpr auto draftTypeKey = "feedback.draft.type";
constexpr auto draftTitleKey = "feedback.draft.title";
constexpr auto draftDescriptionKey = "feedback.draft.description";
constexpr auto draftReproductionKey = "feedback.draft.reproduction";
constexpr auto draftEmailKey = "feedback.draft.email";
constexpr auto installationIdKey = "feedback.installationId";

#ifndef PRACTICE_TAKES_FEEDBACK_ENDPOINT
#define PRACTICE_TAKES_FEEDBACK_ENDPOINT ""
#endif

juce::String typeName(int type)
{
    switch (type)
    {
    case 1:
        return "Bug";
    case 2:
        return "Usability problem";
    case 3:
        return "Feature request";
    case 4:
        return "Audio problem";
    case 5:
        return "Notation/MIDI problem";
    default:
        return "Other";
    }
}

juce::String contractCategory(int type)
{
    if (type == 2)
        return "usability";
    if (type == 3)
        return "idea";
    if (type == 6)
        return "other";
    return "bug";
}

bool looksLikeEmail(const juce::String& email)
{
    if (email.isEmpty())
        return true;
    const auto at = email.indexOfChar('@');
    return at > 0 && email.indexOfChar(at + 1, '.') > at + 1 && !email.containsAnyOf(" \t\r\n");
}

class FeedbackPreview final : public juce::Component
{
  public:
    FeedbackPreview(const juce::String& preview, const juce::Image& screenshot)
    {
        previewText.setMultiLine(true);
        previewText.setReadOnly(true);
        previewText.setText(preview, false);
        addAndMakeVisible(previewText);
        screenshotView.setImage(screenshot, juce::RectanglePlacement::centred |
                                                juce::RectanglePlacement::onlyReduceInSize);
        addAndMakeVisible(screenshotView);
        setSize(900, 700);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(14);
        previewText.setBounds(bounds.removeFromTop(230));
        bounds.removeFromTop(12);
        screenshotView.setBounds(bounds);
    }

  private:
    juce::TextEditor previewText;
    juce::ImageComponent screenshotView;
};
} // namespace

FeedbackComponent::FeedbackComponent(juce::PropertiesFile& propertiesFile)
    : Thread("Feedback submission"), properties(propertiesFile)
{
    setTitle("Send feedback form");
    setDescription("Submit product feedback without leaving Practice Takes");

    typeLabel.setText("Feedback type (required)", juce::dontSendNotification);
    addAndMakeVisible(typeLabel);
    for (const auto& item :
         std::array<juce::String, 6>{"Bug", "Usability problem", "Feature request", "Audio problem",
                                     "Notation/MIDI problem", "Other"})
        typeBox.addItem(item, typeBox.getNumItems() + 1);
    typeBox.setSelectedId(1, juce::dontSendNotification);
    typeBox.setTitle("Feedback type, required");
    typeBox.setExplicitFocusOrder(1);
    addAndMakeVisible(typeBox);

    titleLabel.setText("Short title (required, 120 characters maximum)",
                       juce::dontSendNotification);
    descriptionLabel.setText("Detailed description (required, 6,500 characters maximum)",
                             juce::dontSendNotification);
    reproductionLabel.setText("Reproduction steps (optional)", juce::dontSendNotification);
    emailLabel.setText("Contact email (optional)", juce::dontSendNotification);
    for (auto* label : {&titleLabel, &descriptionLabel, &reproductionLabel, &emailLabel})
        addAndMakeVisible(label);

    configureEditor(titleEditor, "Short title, required", 2, false);
    configureEditor(descriptionEditor, "Detailed description, required", 3, true);
    configureEditor(reproductionEditor, "Reproduction steps, optional", 4, true);
    configureEditor(emailEditor, "Contact email, optional", 5, false);
    titleEditor.setInputRestrictions(maximumTitleLength);
    descriptionEditor.setInputRestrictions(maximumDescriptionLength);
    reproductionEditor.setInputRestrictions(maximumReproductionLength);
    emailEditor.setInputRestrictions(maximumEmailLength);

    environmentLabel.setText("Optional diagnostics (disabled by default; select for this report)",
                             juce::dontSendNotification);
    environmentLabel.setTitle("Optional diagnostic information");
    addAndMakeVisible(environmentLabel);
    versionDiagnostic.setTitle("Include application version in this submission");
    operatingSystemDiagnostic.setTitle("Include operating system in this submission");
    screenshotDiagnostic.setTitle(
        "Capture all open Practice Takes windows except this feedback window");
    addAndMakeVisible(versionDiagnostic);
    addAndMakeVisible(operatingSystemDiagnostic);
    addAndMakeVisible(screenshotDiagnostic);

    validationLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    validationLabel.setTitle("Submission status");
    validationLabel.setDescription("Feedback validation and submission status");
    addAndMakeVisible(validationLabel);

    previewButton.setExplicitFocusOrder(6);
    previewButton.setTitle("Preview feedback submission");
    previewButton.onClick = [this] { preview(); };
    addAndMakeVisible(previewButton);

    submitButton.setExplicitFocusOrder(7);
    submitButton.setTitle("Submit feedback");
    submitButton.onClick = [this] { submit(); };
    addAndMakeVisible(submitButton);

    restoreDraft();
}

FeedbackComponent::~FeedbackComponent()
{
    signalThreadShouldExit();
    stopThread(3000);
}

void FeedbackComponent::configureEditor(juce::TextEditor& editor,
                                        const juce::String& accessibleName, int focusOrder,
                                        bool multiline)
{
    editor.setMultiLine(multiline);
    editor.setReturnKeyStartsNewLine(multiline);
    editor.setTitle(accessibleName);
    editor.setDescription(accessibleName);
    editor.setExplicitFocusOrder(focusOrder);
    addAndMakeVisible(editor);
}

void FeedbackComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void FeedbackComponent::resized()
{
    auto bounds = getLocalBounds().reduced(22);
    const auto place = [&bounds](juce::Label& label, juce::Component& editor, int height)
    {
        label.setBounds(bounds.removeFromTop(24));
        editor.setBounds(bounds.removeFromTop(height));
        bounds.removeFromTop(8);
    };
    place(typeLabel, typeBox, 32);
    place(titleLabel, titleEditor, 34);
    place(descriptionLabel, descriptionEditor, 132);
    place(reproductionLabel, reproductionEditor, 88);
    place(emailLabel, emailEditor, 34);
    environmentLabel.setBounds(bounds.removeFromTop(26));
    auto diagnosticRow = bounds.removeFromTop(30);
    versionDiagnostic.setBounds(diagnosticRow.removeFromLeft(190));
    operatingSystemDiagnostic.setBounds(diagnosticRow.removeFromLeft(190));
    screenshotDiagnostic.setBounds(diagnosticRow.removeFromLeft(230));
    validationLabel.setBounds(bounds.removeFromTop(30));
    auto buttons = bounds.removeFromTop(36);
    submitButton.setBounds(buttons.removeFromRight(160));
    buttons.removeFromRight(8);
    previewButton.setBounds(buttons.removeFromRight(110));
}

FeedbackComponent::Draft FeedbackComponent::currentDraft() const
{
    return {typeBox.getSelectedId(),
            titleEditor.getText().trim(),
            descriptionEditor.getText().trim(),
            reproductionEditor.getText().trim(),
            emailEditor.getText().trim(),
            versionDiagnostic.getToggleState(),
            operatingSystemDiagnostic.getToggleState(),
            screenshotDiagnostic.getToggleState(),
            {}};
}

juce::String FeedbackComponent::validate() const
{
    const auto draft = currentDraft();
    if (draft.title.isEmpty())
        return "Enter a short title.";
    if (draft.description.length() < 3)
        return "Enter a detailed description of at least 3 characters.";
    if (draft.title.length() > maximumTitleLength ||
        draft.description.length() > maximumDescriptionLength ||
        draft.reproductionSteps.length() > maximumReproductionLength)
        return "One or more fields exceed the displayed size limit.";
    if (previewText(draft).length() > 8000)
        return "The complete submission is too large. Shorten the description or steps.";
    if (!looksLikeEmail(draft.contactEmail))
        return "Enter a valid contact email or leave it blank.";
    return {};
}

juce::String FeedbackComponent::previewText(const Draft& draft) const
{
    auto text = "Type: " + typeName(draft.type) + "\nTitle: " + draft.title + "\n\nDescription:\n" +
                draft.description;
    if (draft.reproductionSteps.isNotEmpty())
        text += "\n\nReproduction steps:\n" + draft.reproductionSteps;
    text += "\n\nContact: " +
            (draft.contactEmail.isEmpty() ? juce::String("Not provided") : draft.contactEmail);
    if (draft.includeVersion || draft.includeOperatingSystem)
    {
        text += "\n\nOptional diagnostics (included with consent):";
        if (draft.includeVersion)
            text += "\nApplication version: " + juce::String(ProjectInfo::versionString);
        if (draft.includeOperatingSystem)
            text += "\nOperating system: " + juce::SystemStats::getOperatingSystemName();
    }
    if (draft.includeScreenshot)
        text += "\n\nAttachment: screenshot of all visible Practice Takes windows "
                "except this feedback window";
    return text;
}

juce::String FeedbackComponent::captureApplicationScreenshot() const
{
    auto* feedbackWindow = getTopLevelComponent();
    auto& desktop = juce::Desktop::getInstance();
    juce::Array<juce::Component*> windows;
    juce::Rectangle<int> capturedBounds;

    for (int index = 0; index < desktop.getNumComponents(); ++index)
    {
        auto* window = desktop.getComponent(index);
        if (window == nullptr || window == feedbackWindow || !window->isVisible() ||
            window->getWidth() <= 0 || window->getHeight() <= 0)
            continue;

        windows.add(window);
        capturedBounds = capturedBounds.isEmpty()
                             ? window->getScreenBounds()
                             : capturedBounds.getUnion(window->getScreenBounds());
    }

    if (windows.isEmpty() || capturedBounds.isEmpty())
        return {};

    constexpr float maximumWidth = 1280.0f;
    constexpr float maximumHeight = 720.0f;
    const auto scale =
        juce::jmin(1.0f, maximumWidth / static_cast<float>(capturedBounds.getWidth()),
                   maximumHeight / static_cast<float>(capturedBounds.getHeight()));
    const auto imageWidth = juce::jmax(1, juce::roundToInt(capturedBounds.getWidth() * scale));
    const auto imageHeight = juce::jmax(1, juce::roundToInt(capturedBounds.getHeight() * scale));
    juce::Image image(juce::Image::RGB, imageWidth, imageHeight, true);
    juce::Graphics graphics(image);
    graphics.fillAll(juce::Colours::black);

    for (auto* window : windows)
    {
        const auto snapshot =
            window->createComponentSnapshot(window->getLocalBounds(), true, scale);
        const auto screenBounds = window->getScreenBounds();
        const auto x = juce::roundToInt((screenBounds.getX() - capturedBounds.getX()) * scale);
        const auto y = juce::roundToInt((screenBounds.getY() - capturedBounds.getY()) * scale);
        graphics.drawImageAt(snapshot, x, y);
    }

    juce::MemoryOutputStream output;
    juce::JPEGImageFormat jpeg;
    jpeg.setQuality(0.72f);
    if (!jpeg.writeImageToStream(image, output))
        return {};

    return juce::Base64::toBase64(output.getData(), output.getDataSize());
}

void FeedbackComponent::preview()
{
    const auto error = validate();
    if (error.isNotEmpty())
    {
        setSubmissionState(SubmissionState::failed, error);
        return;
    }
    auto draft = currentDraft();
    if (!draft.includeScreenshot)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                               "Feedback preview", previewText(draft));
        return;
    }

    draft.screenshotBase64 = captureApplicationScreenshot();
    juce::MemoryOutputStream decoded;
    if (draft.screenshotBase64.isEmpty() ||
        !juce::Base64::convertFromBase64(decoded, draft.screenshotBase64))
    {
        setSubmissionState(SubmissionState::failed,
                           "The application screenshot could not be previewed.");
        return;
    }

    const auto screenshot =
        juce::ImageFileFormat::loadFrom(decoded.getData(), decoded.getDataSize());
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new FeedbackPreview(previewText(draft), screenshot));
    options.dialogTitle = "Feedback preview";
    options.dialogBackgroundColour = findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void FeedbackComponent::submit()
{
    const auto error = validate();
    if (error.isNotEmpty())
    {
        setSubmissionState(SubmissionState::failed, error);
        return;
    }
    if (isThreadRunning())
        return;
    pendingDraft = currentDraft();
    if (pendingDraft.includeScreenshot)
    {
        pendingDraft.screenshotBase64 = captureApplicationScreenshot();
        if (pendingDraft.screenshotBase64.isEmpty())
        {
            setSubmissionState(SubmissionState::failed,
                               "The application screenshot could not be captured.");
            return;
        }
    }
    saveDraft(pendingDraft);
    setSubmissionState(SubmissionState::submitting, "Submitting feedback...");
    startThread();
}

void FeedbackComponent::setSubmissionState(SubmissionState state, const juce::String& detail)
{
    submissionState = state;
    validationLabel.setText(detail, juce::dontSendNotification);
    submitButton.setEnabled(state != SubmissionState::submitting);
}

void FeedbackComponent::saveDraft(const Draft& draft)
{
    properties.setValue(draftTypeKey, draft.type);
    properties.setValue(draftTitleKey, draft.title);
    properties.setValue(draftDescriptionKey, draft.description);
    properties.setValue(draftReproductionKey, draft.reproductionSteps);
    properties.setValue(draftEmailKey, draft.contactEmail);
    properties.saveIfNeeded();
}

void FeedbackComponent::clearDraft()
{
    for (const auto* key :
         {draftTypeKey, draftTitleKey, draftDescriptionKey, draftReproductionKey, draftEmailKey})
        properties.removeValue(key);
    properties.saveIfNeeded();
}

void FeedbackComponent::restoreDraft()
{
    typeBox.setSelectedId(properties.getIntValue(draftTypeKey, 1), juce::dontSendNotification);
    titleEditor.setText(properties.getValue(draftTitleKey), false);
    descriptionEditor.setText(properties.getValue(draftDescriptionKey), false);
    reproductionEditor.setText(properties.getValue(draftReproductionKey), false);
    emailEditor.setText(properties.getValue(draftEmailKey), false);
    versionDiagnostic.setToggleState(false, juce::dontSendNotification);
    operatingSystemDiagnostic.setToggleState(false, juce::dontSendNotification);
    screenshotDiagnostic.setToggleState(false, juce::dontSendNotification);
}

void FeedbackComponent::run()
{
    const juce::String endpoint{PRACTICE_TAKES_FEEDBACK_ENDPOINT};
    if (endpoint.isEmpty())
    {
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer(this)]
            {
                if (safe != nullptr)
                    safe->setSubmissionState(
                        SubmissionState::queued,
                        "Queued locally; the feedback service is unavailable.");
            });
        return;
    }

    auto installationId = properties.getValue(installationIdKey);
    if (installationId.isEmpty())
    {
        installationId = juce::Uuid().toDashedString().removeCharacters("-");
        properties.setValue(installationIdKey, installationId);
        properties.saveIfNeeded();
    }

    const auto appVersion = juce::String(ProjectInfo::versionString);
    auto authorizationBody = juce::JSON::toString(juce::var(new juce::DynamicObject));
    auto* authorizationObject = new juce::DynamicObject;
    authorizationObject->setProperty("schemaVersion", 1);
    authorizationObject->setProperty("appVersion", appVersion);
    authorizationObject->setProperty("installationId", installationId);
    authorizationBody = juce::JSON::toString(juce::var(authorizationObject));

    int authorizationStatus = 0;
    const auto authorizationResponse =
        juce::URL(endpoint + "/v1/authorizations")
            .withPOSTData(authorizationBody)
            .createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders("Content-Type: application/json\r\n")
                    .withStatusCode(&authorizationStatus));
    if (authorizationResponse == nullptr || authorizationStatus != 201)
    {
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer(this)]
            {
                if (safe != nullptr)
                    safe->setSubmissionState(SubmissionState::queued,
                                             "Queued locally; submission will need to be retried.");
            });
        return;
    }

    const auto authorizationJson =
        juce::JSON::parse(authorizationResponse->readEntireStreamAsString());
    const auto authorization = authorizationJson.getProperty("authorization", {}).toString();
    auto* submissionObject = new juce::DynamicObject;
    submissionObject->setProperty("schemaVersion", 1);
    submissionObject->setProperty("authorization", authorization);
    submissionObject->setProperty("submittedAt", juce::Time::getCurrentTime().toISO8601(true));
    submissionObject->setProperty("appVersion", appVersion);
    submissionObject->setProperty("installationId", installationId);
    submissionObject->setProperty("category", contractCategory(pendingDraft.type));
    submissionObject->setProperty("message", previewText(pendingDraft));
    if (pendingDraft.contactEmail.isNotEmpty())
        submissionObject->setProperty("contactEmail", pendingDraft.contactEmail);
    if (pendingDraft.screenshotBase64.isNotEmpty())
    {
        submissionObject->setProperty("screenshotMimeType", "image/jpeg");
        submissionObject->setProperty("screenshotBase64", pendingDraft.screenshotBase64);
    }

    int submissionStatus = 0;
    const auto submissionResponse =
        juce::URL(endpoint + "/v1/submissions")
            .withPOSTData(juce::JSON::toString(juce::var(submissionObject)))
            .createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders("Content-Type: application/json\r\n")
                    .withStatusCode(&submissionStatus));
    juce::String submissionError;
    if (submissionResponse != nullptr && submissionStatus != 201)
    {
        const auto responseJson = juce::JSON::parse(submissionResponse->readEntireStreamAsString());
        submissionError = responseJson.getProperty("message", {}).toString();
    }
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer(this), submissionStatus, submissionError]
        {
            if (safe == nullptr)
                return;
            if (submissionStatus == 201)
            {
                safe->clearDraft();
                safe->titleEditor.clear();
                safe->descriptionEditor.clear();
                safe->reproductionEditor.clear();
                safe->emailEditor.clear();
                safe->versionDiagnostic.setToggleState(false, juce::dontSendNotification);
                safe->operatingSystemDiagnostic.setToggleState(false, juce::dontSendNotification);
                safe->screenshotDiagnostic.setToggleState(false, juce::dontSendNotification);
                safe->setSubmissionState(SubmissionState::succeeded,
                                         "Feedback submitted successfully. Thank you.");
            }
            else
            {
                auto detail = "Submission failed (HTTP " + juce::String(submissionStatus) + ").";
                if (submissionError.isNotEmpty())
                    detail += " " + submissionError;
                detail += " Your draft has been preserved.";
                safe->setSubmissionState(SubmissionState::failed, detail);
            }
        });
}
