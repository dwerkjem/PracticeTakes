#pragma once

#include "../../application/localisation/Localisation.h"
#include "engraving/ScoreLayout.h"

// What a layout notice says, in the interface's language: the engraving pass
// keeps its findings as data and composes no prose.
namespace score_view
{
[[nodiscard]] inline juce::String noticeText(const engraving::Notice& notice)
{
    l10n::NamedArguments arguments = notice.arguments;
    for (const auto& [name, phrase] : notice.phraseArguments)
    {
        arguments.emplace_back(name, l10n::text(phrase).toStdString());
    }
    return notice.count.has_value() ? l10n::pluralArguments(notice.code, *notice.count, arguments)
                                    : l10n::formatArguments(notice.code, arguments);
}
} // namespace score_view
