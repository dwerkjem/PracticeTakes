#include "FeedbackComponent.h"
#include "../../application/localisation/Localisation.h"

#include <string_view>

namespace
{
constexpr int maximumTitleLength = 120;
constexpr int maximumDescriptionLength = 6500;
constexpr int maximumReproductionLength = 1200;
constexpr int maximumEmailLength = 254;
constexpr int requestTimeoutMs = 10000;

constexpr auto draftTypeKey = "feedback.draft.type";
constexpr auto draftTitleKey = "feedback.draft.title";
constexpr auto draftDescriptionKey = "feedback.draft.description";
constexpr auto draftReproductionKey = "feedback.draft.reproduction";
constexpr auto draftEmailKey = "feedback.draft.email";
constexpr auto draftContextKey = "feedback.draft.context";
constexpr auto installationIdKey = "feedback.installationId";

#ifndef PRACTICE_TAKES_FEEDBACK_ENDPOINT
#define PRACTICE_TAKES_FEEDBACK_ENDPOINT ""
#endif

constexpr bool hasHttpsScheme(std::string_view endpoint)
{
    constexpr std::string_view scheme{"https://"};
    if (endpoint.length() < scheme.length())
    {
        return false;
    }
    for (std::string_view::size_type index = 0; index < scheme.length(); ++index)
    {
        auto character = endpoint[index];
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != scheme[index])
        {
            return false;
        }
    }
    return true;
}

static_assert(
    std::string_view{PRACTICE_TAKES_FEEDBACK_ENDPOINT}.empty() ||
        hasHttpsScheme(PRACTICE_TAKES_FEEDBACK_ENDPOINT),
    "PRACTICE_TAKES_FEEDBACK_ENDPOINT must be an https origin");

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
    {
        return "usability";
    }
    if (type == 3)
    {
        return "idea";
    }
    if (type == 6)
    {
        return "other";
    }
    return "bug";
}

bool looksLikeEmail(const juce::String& email)
{
    if (email.isEmpty())
    {
        return true;
    }
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
        screenshotView.setImage(
            screenshot,
            juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
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

FeedbackComponent::FeedbackComponent(
    juce::PropertiesFile& propertiesFile,
    const juce::String& initialContext)
    : Thread("Feedback submission"), properties(propertiesFile)
{
    setTitle(l10n::text("feedback.form.title"));
    setDescription(l10n::text("feedback.form.description"));

    typeLabel.setText(l10n::text("feedback.type.label"), juce::dontSendNotification);
    addAndMakeVisible(typeLabel);
    // In the order of typeName's numbering. Shown translated; the message
    // the maintainer receives names the type in English (typeName).
    for (const auto item :
         {l10n::key("feedback.type.bug"), l10n::key("feedback.type.usability"),
          l10n::key("feedback.type.feature"), l10n::key("feedback.type.audio"),
          l10n::key("feedback.type.notation"), l10n::key("feedback.type.other")})
    {
        typeBox.addItem(l10n::text(item), typeBox.getNumItems() + 1);
    }
    typeBox.setSelectedId(1, juce::dontSendNotification);
    typeBox.setTitle(l10n::text("feedback.type.title"));
    typeBox.setExplicitFocusOrder(1);
    addAndMakeVisible(typeBox);

    titleLabel.setText(
        l10n::format(
            "feedback.title.label",
            {{"most", l10n::number(static_cast<double>(maximumTitleLength), 0)}}),
        juce::dontSendNotification);
    descriptionLabel.setText(
        l10n::format(
            "feedback.description.label",
            {{"most", l10n::number(static_cast<double>(maximumDescriptionLength), 0)}}),
        juce::dontSendNotification);
    reproductionLabel.setText(
        l10n::text("feedback.reproduction.label"), juce::dontSendNotification);
    emailLabel.setText(l10n::text("feedback.email.label"), juce::dontSendNotification);
    contextLabel.setText(l10n::text("feedback.context.label"), juce::dontSendNotification);
    for (auto* label :
         {&titleLabel, &descriptionLabel, &reproductionLabel, &emailLabel, &contextLabel})
    {
        addAndMakeVisible(label);
    }

    configureEditor(titleEditor, l10n::text("feedback.title.title"), 2, false);
    configureEditor(descriptionEditor, l10n::text("feedback.description.title"), 3, true);
    configureEditor(reproductionEditor, l10n::text("feedback.reproduction.title"), 4, true);
    configureEditor(emailEditor, l10n::text("feedback.email.title"), 5, false);
    configureEditor(contextEditor, l10n::text("feedback.context.title"), 6, true);
    titleEditor.setInputRestrictions(maximumTitleLength);
    descriptionEditor.setInputRestrictions(maximumDescriptionLength);
    reproductionEditor.setInputRestrictions(maximumReproductionLength);
    emailEditor.setInputRestrictions(maximumEmailLength);

    environmentLabel.setText(l10n::text("feedback.diagnostics.label"), juce::dontSendNotification);
    environmentLabel.setTitle(l10n::text("feedback.diagnostics.title"));
    environmentLabel.setDescription(l10n::text("feedback.diagnostics.description"));
    addAndMakeVisible(environmentLabel);
    versionDiagnostic.setTitle(l10n::text("feedback.diagnostics.version.title"));
    operatingSystemDiagnostic.setTitle(l10n::text("feedback.diagnostics.os.title"));
    screenshotDiagnostic.setTitle(l10n::text("feedback.diagnostics.screenshot.title"));
    addAndMakeVisible(versionDiagnostic);
    addAndMakeVisible(operatingSystemDiagnostic);
    addAndMakeVisible(screenshotDiagnostic);

    validationLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    validationLabel.setTitle(l10n::text("feedback.status.title"));
    validationLabel.setDescription(l10n::text("feedback.status.description"));
    addAndMakeVisible(validationLabel);

    previewButton.setExplicitFocusOrder(7);
    previewButton.setTitle(l10n::text("feedback.preview.title"));
    previewButton.onClick = [this] { preview(); };
    addAndMakeVisible(previewButton);

    submitButton.setExplicitFocusOrder(8);
    submitButton.setTitle(l10n::text("feedback.submit"));
    submitButton.onClick = [this] { submit(); };
    addAndMakeVisible(submitButton);

    restoreDraft();
    if (initialContext.isNotEmpty())
    {
        setContextTag(initialContext);
    }
}

FeedbackComponent::~FeedbackComponent()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock lock(requestLock);
        if (activeRequest != nullptr)
        {
            activeRequest->cancel();
        }
    }
    stopThread(requestTimeoutMs);
}

void FeedbackComponent::configureEditor(
    juce::TextEditor& editor,
    const juce::String& accessibleName,
    int focusOrder,
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

void FeedbackComponent::lookAndFeelChanged()
{
    // Read from the look and feel rather than through the editor's own
    // findColour, and applied without taking the colour as the editor's own: a
    // Component's colour map wins over the look and feel, so pinning it here
    // would freeze the form at today's palette and make the next theme change
    // do nothing.
    const juce::Colour text = getLookAndFeel().findColour(juce::TextEditor::textColourId);

    for (juce::TextEditor* editor :
         {&titleEditor, &descriptionEditor, &reproductionEditor, &emailEditor, &contextEditor})
    {
        editor->applyColourToAllText(text, false);
    }
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
    place(contextLabel, contextEditor, 52);
    environmentLabel.setBounds(bounds.removeFromTop(44));
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
    return {
        typeBox.getSelectedId(),
        titleEditor.getText().trim(),
        descriptionEditor.getText().trim(),
        reproductionEditor.getText().trim(),
        emailEditor.getText().trim(),
        contextEditor.getText().trim(),
        versionDiagnostic.getToggleState(),
        operatingSystemDiagnostic.getToggleState(),
        screenshotDiagnostic.getToggleState(),
        {},
        {}};
}

juce::String FeedbackComponent::validate() const
{
    const auto draft = currentDraft();
    if (draft.title.isEmpty())
    {
        return l10n::text("feedback.invalid.no-title");
    }
    if (draft.description.length() < 3)
    {
        return l10n::text("feedback.invalid.short-description");
    }
    if (draft.title.length() > maximumTitleLength ||
        draft.description.length() > maximumDescriptionLength ||
        draft.reproductionSteps.length() > maximumReproductionLength)
    {
        return l10n::text("feedback.invalid.field-too-long");
    }
    if (previewText(draft).length() > 8000)
    {
        return l10n::text("feedback.invalid.too-large");
    }
    if (!looksLikeEmail(draft.contactEmail))
    {
        return l10n::text("feedback.invalid.email");
    }
    return {};
}

// The message the maintainer receives, which the preview shows the sender as
// it will be sent. In English whatever the interface language: its reader is
// the maintainer (docs/development/architecture/localisation.md).
juce::String FeedbackComponent::previewText(const Draft& draft) const
{
    auto text = "Type: " + typeName(draft.type) + "\nTitle: " + draft.title + "\n\nDescription:\n" +
                draft.description;
    if (draft.reproductionSteps.isNotEmpty())
    {
        text += "\n\nReproduction steps:\n" + draft.reproductionSteps;
    }
    text += "\n\nContact: " +
            (draft.contactEmail.isEmpty() ? juce::String("Not provided") : draft.contactEmail);
    if (draft.includeVersion || draft.includeOperatingSystem)
    {
        text += "\n\nOptional diagnostics (included with consent):";
        if (draft.includeVersion)
        {
            text += "\nApplication version: " + juce::String(ProjectInfo::versionString);
        }
        if (draft.includeOperatingSystem)
        {
            text += "\nOperating system: " + juce::SystemStats::getOperatingSystemName();
        }
    }
    if (draft.includeScreenshot)
    {
        text += "\n\nAttachment: screenshot of all visible Practice Takes windows "
                "except this feedback window";
    }
    if (draft.contextTag.isNotEmpty())
    {
        text += "\n\nFeedback context (user-editable):\n" + draft.contextTag;
    }
    return text;
}

juce::String FeedbackComponent::previewSummary(const Draft& draft) const
{
    return previewText(draft) + "\n\n" +
           l10n::format(
               "feedback.preview.always-sent",
               {{"version", juce::String(ProjectInfo::versionString)}});
}

void FeedbackComponent::setContextTag(const juce::String& toolOrWorkflowName)
{
    contextEditor.setText(
        "Tool or workflow: " + toolOrWorkflowName +
            "\nApplication version: " + juce::String(ProjectInfo::versionString),
        false);
    contextEditor.grabKeyboardFocus();
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
        {
            continue;
        }

        windows.add(window);
        capturedBounds = capturedBounds.isEmpty()
                             ? window->getScreenBounds()
                             : capturedBounds.getUnion(window->getScreenBounds());
    }

    if (windows.isEmpty() || capturedBounds.isEmpty())
    {
        return {};
    }

    constexpr float maximumWidth = 1280.0f;
    constexpr float maximumHeight = 720.0f;
    const auto scale = juce::jmin(
        1.0f, maximumWidth / static_cast<float>(capturedBounds.getWidth()),
        maximumHeight / static_cast<float>(capturedBounds.getHeight()));
    const auto imageWidth =
        juce::jmax(1, juce::roundToInt(static_cast<float>(capturedBounds.getWidth()) * scale));
    const auto imageHeight =
        juce::jmax(1, juce::roundToInt(static_cast<float>(capturedBounds.getHeight()) * scale));
    juce::Image image(juce::Image::RGB, imageWidth, imageHeight, true);
    juce::Graphics graphics(image);
    graphics.fillAll(juce::Colours::black);

    for (auto* window : windows)
    {
        const auto snapshot =
            window->createComponentSnapshot(window->getLocalBounds(), true, scale);
        const auto screenBounds = window->getScreenBounds();
        const auto x = juce::roundToInt(
            static_cast<float>(screenBounds.getX() - capturedBounds.getX()) * scale);
        const auto y = juce::roundToInt(
            static_cast<float>(screenBounds.getY() - capturedBounds.getY()) * scale);
        graphics.drawImageAt(snapshot, x, y);
    }

    juce::MemoryOutputStream output;
    juce::JPEGImageFormat jpeg;
    jpeg.setQuality(0.72f);
    if (!jpeg.writeImageToStream(image, output))
    {
        return {};
    }

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
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon, l10n::text("feedback.preview"),
            previewSummary(draft));
        return;
    }

    draft.screenshotBase64 = captureApplicationScreenshot();
    juce::MemoryOutputStream decoded;
    if (draft.screenshotBase64.isEmpty() ||
        !juce::Base64::convertFromBase64(decoded, draft.screenshotBase64))
    {
        setSubmissionState(
            SubmissionState::failed, l10n::text("feedback.status.no-screenshot-preview"));
        return;
    }

    const auto screenshot =
        juce::ImageFileFormat::loadFrom(decoded.getData(), decoded.getDataSize());
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new FeedbackPreview(previewSummary(draft), screenshot));
    options.dialogTitle = l10n::text("feedback.preview");
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
    {
        return;
    }
    pendingDraft = currentDraft();
    pendingDraft.clientSubmissionId = juce::Uuid().toDashedString();
    if (pendingDraft.includeScreenshot)
    {
        pendingDraft.screenshotBase64 = captureApplicationScreenshot();
        if (pendingDraft.screenshotBase64.isEmpty())
        {
            setSubmissionState(
                SubmissionState::failed, l10n::text("feedback.status.no-screenshot"));
            return;
        }
    }
    saveDraft(pendingDraft);
    setSubmissionState(SubmissionState::submitting, l10n::text("feedback.status.submitting"));
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
    if (draft.contactEmail.isEmpty())
    {
        properties.removeValue(draftEmailKey);
    }
    else
    {
        properties.setValue(draftEmailKey, draft.contactEmail);
    }
    properties.setValue(draftContextKey, draft.contextTag);
    properties.saveIfNeeded();
}

void FeedbackComponent::clearDraft()
{
    for (const auto* key :
         {draftTypeKey, draftTitleKey, draftDescriptionKey, draftReproductionKey, draftEmailKey,
          draftContextKey})
    {
        properties.removeValue(key);
    }
    properties.saveIfNeeded();
}

void FeedbackComponent::restoreDraft()
{
    typeBox.setSelectedId(properties.getIntValue(draftTypeKey, 1), juce::dontSendNotification);
    titleEditor.setText(properties.getValue(draftTitleKey), false);
    descriptionEditor.setText(properties.getValue(draftDescriptionKey), false);
    reproductionEditor.setText(properties.getValue(draftReproductionKey), false);
    emailEditor.setText(properties.getValue(draftEmailKey), false);
    contextEditor.setText(properties.getValue(draftContextKey), false);
    versionDiagnostic.setToggleState(false, juce::dontSendNotification);
    operatingSystemDiagnostic.setToggleState(false, juce::dontSendNotification);
    screenshotDiagnostic.setToggleState(false, juce::dontSendNotification);
}

bool FeedbackComponent::sendRequest(
    juce::WebInputStream& request,
    int& statusCode,
    juce::String& responseBody)
{
    request.withExtraHeaders("Content-Type: application/json\r\n")
        .withConnectionTimeout(requestTimeoutMs)
        .withNumRedirectsToFollow(0);
    {
        const juce::ScopedLock lock(requestLock);
        if (threadShouldExit())
        {
            return false;
        }
        activeRequest = &request;
    }

    const auto connected = request.connect(nullptr) && !request.isError();
    if (connected)
    {
        statusCode = request.getStatusCode();
        responseBody = request.readEntireStreamAsString();
    }

    const juce::ScopedLock lock(requestLock);
    activeRequest = nullptr;
    return connected && !threadShouldExit();
}

void FeedbackComponent::run()
{
    const juce::String endpoint{PRACTICE_TAKES_FEEDBACK_ENDPOINT};
    if (endpoint.isEmpty() || !endpoint.startsWithIgnoreCase("https://"))
    {
        // Chosen here, rendered on the message thread, which owns the
        // catalogue.
        const auto detail = endpoint.isNotEmpty() ? l10n::key("feedback.status.queued.insecure")
                                                  : l10n::key("feedback.status.queued.unavailable");
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer(this), detail]
            {
                if (safe != nullptr)
                {
                    safe->setSubmissionState(SubmissionState::queued, l10n::text(detail));
                }
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

    juce::WebInputStream authorizationRequest(
        juce::URL(endpoint + "/v1/authorizations").withPOSTData(authorizationBody), false);
    int authorizationStatus = 0;
    juce::String authorizationResponse;
    const auto authorized =
        sendRequest(authorizationRequest, authorizationStatus, authorizationResponse);
    if (threadShouldExit())
    {
        return;
    }
    if (!authorized || authorizationStatus != 201)
    {
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer(this)]
            {
                if (safe != nullptr)
                {
                    safe->setSubmissionState(
                        SubmissionState::queued, l10n::text("feedback.status.queued.retry"));
                }
            });
        return;
    }

    const auto authorizationJson = juce::JSON::parse(authorizationResponse);
    const auto authorization = authorizationJson.getProperty("authorization", {}).toString();
    auto* submissionObject = new juce::DynamicObject;
    submissionObject->setProperty("schemaVersion", 1);
    submissionObject->setProperty("authorization", authorization);
    submissionObject->setProperty("submittedAt", juce::Time::getCurrentTime().toISO8601(true));
    submissionObject->setProperty("appVersion", appVersion);
    submissionObject->setProperty("installationId", installationId);
    submissionObject->setProperty("clientSubmissionId", pendingDraft.clientSubmissionId);
    submissionObject->setProperty("category", contractCategory(pendingDraft.type));
    submissionObject->setProperty("message", previewText(pendingDraft));
    if (pendingDraft.contactEmail.isNotEmpty())
    {
        submissionObject->setProperty("contactEmail", pendingDraft.contactEmail);
    }
    if (pendingDraft.screenshotBase64.isNotEmpty())
    {
        submissionObject->setProperty("screenshotMimeType", "image/jpeg");
        submissionObject->setProperty("screenshotBase64", pendingDraft.screenshotBase64);
    }

    juce::WebInputStream submissionRequest(
        juce::URL(endpoint + "/v1/submissions")
            .withPOSTData(juce::JSON::toString(juce::var(submissionObject))),
        false);
    int submissionStatus = 0;
    juce::String submissionResponse;
    const auto submitted = sendRequest(submissionRequest, submissionStatus, submissionResponse);
    if (threadShouldExit())
    {
        return;
    }
    juce::String submissionError;
    const auto submissionSucceeded = submissionStatus == 200 || submissionStatus == 201;
    if (submitted && !submissionSucceeded)
    {
        const auto responseJson = juce::JSON::parse(submissionResponse);
        const auto errorJson = responseJson.getProperty("error", {});
        submissionError = errorJson.getProperty("message", {}).toString();
    }
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer(this), submissionStatus, submissionError,
         submissionSucceeded]
        {
            if (safe == nullptr)
            {
                return;
            }
            if (submissionSucceeded)
            {
                safe->clearDraft();
                safe->titleEditor.clear();
                safe->descriptionEditor.clear();
                safe->reproductionEditor.clear();
                safe->emailEditor.clear();
                safe->contextEditor.clear();
                safe->versionDiagnostic.setToggleState(false, juce::dontSendNotification);
                safe->operatingSystemDiagnostic.setToggleState(false, juce::dontSendNotification);
                safe->screenshotDiagnostic.setToggleState(false, juce::dontSendNotification);
                safe->setSubmissionState(
                    SubmissionState::succeeded, l10n::text("feedback.status.submitted"));
            }
            else
            {
                // The service's own explanation is its operator's English,
                // passed on as it came.
                const auto detail =
                    submissionError.isNotEmpty()
                        ? l10n::format(
                              "feedback.status.failed.because",
                              {{"status", juce::String(submissionStatus)},
                               {"reason", submissionError}})
                        : l10n::format(
                              "feedback.status.failed",
                              {{"status", juce::String(submissionStatus)}});
                safe->setSubmissionState(SubmissionState::failed, detail);
            }
        });
}
