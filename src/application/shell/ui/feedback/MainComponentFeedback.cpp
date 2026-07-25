#include "../../MainComponent.h"

#include "../../../../features/feedback/FeedbackInvitationPolicy.h"
#include "FeedbackWindow.h"

namespace
{
constexpr int helpMenuWidth = 190;
constexpr int sendFeedbackMenuItemId = 1;
constexpr int feedbackInvitationsMenuItemId = 2;
constexpr auto feedbackSuccessfulUsesKey = "feedback.successfulToolUses";
constexpr auto feedbackInvitationShownKey = "feedback.invitationShown";
constexpr auto feedbackInvitationsDisabledKey = "feedback.invitationsDisabled";
} // namespace

void MainComponent::showHelpMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addItem(sendFeedbackMenuItemId, "Send feedback");
    menu.addSeparator();
    menu.addItem(
        feedbackInvitationsMenuItemId,
        feedbackInvitationsDisabled() ? "Enable feedback invitations"
                                      : "Disable feedback invitations");

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&helpButton).withMinimumWidth(helpMenuWidth),
        [safeThis](int selectedItemId)
        {
            if (safeThis == nullptr)
                return;
            if (selectedItemId == sendFeedbackMenuItemId)
                safeThis->showFeedback();
            else if (selectedItemId == feedbackInvitationsMenuItemId)
                safeThis->setFeedbackInvitationsDisabled(!safeThis->feedbackInvitationsDisabled());
        });
}

void MainComponent::showFeedback(const juce::String& context)
{
    if (feedbackWindow != nullptr)
    {
        if (context.isNotEmpty())
            feedbackWindow->setContextTag(context);
        feedbackWindow->setVisible(true);
        feedbackWindow->toFront(true);
        return;
    }

    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    feedbackWindow = std::make_unique<FeedbackWindow>(
        *settingsFile, context,
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->closeFeedback();
        });
    feedbackWindow->setLookAndFeel(&appLookAndFeel);
    feedbackWindow->toFront(true);
}

void MainComponent::closeFeedback()
{
    feedbackWindow.reset();
}

void MainComponent::recordSuccessfulToolUse()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const auto uses = settingsFile->getIntValue(feedbackSuccessfulUsesKey, 0) + 1;
    settingsFile->setValue(feedbackSuccessfulUsesKey, uses);
    settingsFile->saveIfNeeded();
    maybeOfferFeedbackInvitation();
}

void MainComponent::maybeOfferFeedbackInvitation()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const FeedbackInvitationPolicy::State state{
        settingsFile->getIntValue(feedbackSuccessfulUsesKey, 0),
        settingsFile->getBoolValue(feedbackInvitationShownKey, false),
        settingsFile->getBoolValue(feedbackInvitationsDisabledKey, false)};
    const auto isLiveSessionActive = tunerState.isOpen() || spectrogramState.isOpen();
    if (!FeedbackInvitationPolicy::shouldInvite(state, isLiveSessionActive))
        return;

    settingsFile->setValue(feedbackInvitationShownKey, true);
    settingsFile->saveIfNeeded();

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Help improve Practice Takes",
        "Would you like to share feedback about the tools you have tried?", "Give feedback",
        "Not now", "Never ask again", nullptr,
        juce::ModalCallbackFunction::create(
            [safeThis](int result)
            {
                if (safeThis == nullptr)
                    return;
                if (result == 1)
                    safeThis->showFeedback("Early tester experience");
                else if (result == 0)
                    safeThis->setFeedbackInvitationsDisabled(true);
            }));
}

void MainComponent::setFeedbackInvitationsDisabled(bool disabled)
{
    if (auto* settingsFile = applicationProperties.getUserSettings())
    {
        settingsFile->setValue(feedbackInvitationsDisabledKey, disabled);
        if (!disabled)
            settingsFile->setValue(feedbackInvitationShownKey, false);
        settingsFile->saveIfNeeded();
    }
}

bool MainComponent::feedbackInvitationsDisabled()
{
    if (auto* settingsFile = applicationProperties.getUserSettings())
        return settingsFile->getBoolValue(feedbackInvitationsDisabledKey, false);
    return false;
}
