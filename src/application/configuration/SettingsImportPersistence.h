#pragma once

#include "SettingsTransferCodec.h"

#include <juce_data_structures/juce_data_structures.h>

class SettingsImportPersistence
{
  public:
    static constexpr auto feedbackInvitationsDisabledKey = "feedback.invitationsDisabled";

    [[nodiscard]] static bool persist(
        juce::PropertiesFile& current,
        const juce::PropertiesFile::Options& options,
        const SettingsTransferModel& candidate)
    {
        if (!current.saveIfNeeded())
        {
            return false;
        }

        juce::TemporaryFile temporary(current.getFile());
        {
            juce::PropertiesFile staged(temporary.getFile(), options);
            if (!staged.isValidFile())
            {
                return false;
            }
            staged.addAllPropertiesFrom(current);
            AppSettings::store(staged, candidate.state);
            staged.setValue(feedbackInvitationsDisabledKey, candidate.feedbackInvitationsDisabled);
            if (!staged.save())
            {
                return false;
            }
            staged.setNeedsToBeSaved(false);
        }

        if (!temporary.overwriteTargetFileWithTemporary())
        {
            return false;
        }

        AppSettings::store(current, candidate.state);
        current.setValue(feedbackInvitationsDisabledKey, candidate.feedbackInvitationsDisabled);
        current.setNeedsToBeSaved(false);
        return true;
    }
};