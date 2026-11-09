#pragma once

#include "../../../../localisation/LocalisationRules.h"

#include <string_view>

// What the shell says about one action on a named workspace, by catalogue
// identifier: its title, and its outcome. A table rather than one verb
// spliced into shared sentences, since "cannot be {action}" agrees with the
// verb differently in every language.
struct WorkspaceActionText
{
    std::string_view title;
    std::string_view failed;
    std::string_view completed;

    // The completion, naming the workspace: "… for ‘{name}’."
    std::string_view completedFor;

    // Why a built-in workspace refuses it.
    std::string_view builtIn;
};

namespace workspaceactions
{
inline constexpr WorkspaceActionText apply{
    l10n::key("workspace.apply.title"), l10n::key("workspace.apply.failed"),
    l10n::key("workspace.apply.completed"), l10n::key("workspace.apply.completed-for"),
    l10n::key("workspace.apply.built-in")};
inline constexpr WorkspaceActionText save{
    l10n::key("workspace.save.title"), l10n::key("workspace.save.failed"),
    l10n::key("workspace.save.completed"), l10n::key("workspace.save.completed-for"),
    l10n::key("workspace.save.built-in")};
inline constexpr WorkspaceActionText rename{
    l10n::key("workspace.rename.title"), l10n::key("workspace.rename.failed"),
    l10n::key("workspace.rename.completed"), l10n::key("workspace.rename.completed-for"),
    l10n::key("workspace.rename.built-in")};
inline constexpr WorkspaceActionText overwrite{
    l10n::key("workspace.overwrite.title"), l10n::key("workspace.overwrite.failed"),
    l10n::key("workspace.overwrite.completed"), l10n::key("workspace.overwrite.completed-for"),
    l10n::key("workspace.overwrite.built-in")};
inline constexpr WorkspaceActionText remove{
    l10n::key("workspace.delete.title"), l10n::key("workspace.delete.failed"),
    l10n::key("workspace.delete.completed"), l10n::key("workspace.delete.completed-for"),
    l10n::key("workspace.delete.built-in")};
} // namespace workspaceactions
