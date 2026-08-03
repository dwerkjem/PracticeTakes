#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Names one live copy of a tool, as distinct from the tool itself.
//
// A tool id ("tuner") says what kind of tool something is; an instance id says
// which copy. They are deliberately separate concepts even though every tool
// currently ships single-instance, because keeping them separate is what lets
// multi-instance be added later without touching the persisted format or the
// workspace model.
//
// The encoding makes that migration free: the first instance of a tool has an
// instance id byte-identical to the tool id, so everything written today
// deserialises unchanged. A hypothetical second instance would be "tuner#2" --
// the tool id, a '#', and the ordinal. Tool ids may therefore never contain
// '#', which ToolCatalog rejects at registration.
class ToolInstanceId
{
  public:
    ToolInstanceId() = default;

    explicit ToolInstanceId(std::string value) : text(std::move(value)) {}

    // The instance id for the nth instance of a tool, counting from 1. Ordinal
    // 1 is the bare tool id; anything higher carries the "#n" suffix.
    [[nodiscard]] static ToolInstanceId forOrdinal(std::string_view toolId, std::size_t ordinal)
    {
        auto value = std::string(toolId);
        if (ordinal > 1)
        {
            value += '#';
            value += std::to_string(ordinal);
        }
        return ToolInstanceId(std::move(value));
    }

    [[nodiscard]] const std::string& value() const noexcept
    {
        return text;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return text.empty();
    }

    // The tool this instance was created from: everything before the '#'.
    [[nodiscard]] std::string_view toolId() const noexcept
    {
        const auto separator = text.find('#');
        return std::string_view(text).substr(
            0, separator == std::string::npos ? text.size() : separator);
    }

    // Which copy this is, counting from 1. A malformed or non-numeric suffix
    // yields nullopt rather than a guess, so a corrupt saved workspace is
    // rejected by the caller instead of silently colliding with instance 1.
    [[nodiscard]] std::optional<std::size_t> ordinal() const noexcept
    {
        const auto separator = text.find('#');
        if (separator == std::string::npos)
        {
            return text.empty() ? std::nullopt : std::optional<std::size_t>(1);
        }

        const auto digits = std::string_view(text).substr(separator + 1);
        if (digits.empty())
        {
            return std::nullopt;
        }

        std::size_t parsed = 0;
        for (const auto character : digits)
        {
            if (character < '0' || character > '9')
            {
                return std::nullopt;
            }
            parsed = (parsed * 10) + static_cast<std::size_t>(character - '0');
        }

        // "tuner#1" is not a legal spelling of instance 1 -- that is "tuner".
        // Allowing both would give one instance two ids and break the dedup
        // that keeps a single-instance tool single.
        return parsed < 2 ? std::nullopt : std::optional<std::size_t>(parsed);
    }

    [[nodiscard]] bool isWellFormed() const noexcept
    {
        return !text.empty() && !toolId().empty() && ordinal().has_value();
    }

    bool operator==(const ToolInstanceId&) const = default;
    auto operator<=>(const ToolInstanceId&) const = default;

  private:
    std::string text;
};
