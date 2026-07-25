#pragma once

#include <cstdint>

class WorkspaceToolState
{
  public:
    enum class Presentation
    {
        closed,
        docked,
        floating
    };

    struct Transition
    {
        bool presentationChanged = false;
        bool requiresNewInstance = false;
    };

    [[nodiscard]] Transition present(Presentation target) noexcept
    {
        if (target == Presentation::closed)
        {
            return close();
        }

        const auto wasClosed = currentPresentation == Presentation::closed;
        const auto changed = currentPresentation != target;
        currentPresentation = target;
        if (wasClosed)
        {
            ++generation;
        }
        return {changed, wasClosed};
    }

    [[nodiscard]] Transition close() noexcept
    {
        const auto changed = currentPresentation != Presentation::closed;
        currentPresentation = Presentation::closed;
        return {changed, false};
    }

    [[nodiscard]] Presentation presentation() const noexcept
    {
        return currentPresentation;
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return currentPresentation != Presentation::closed;
    }

    // Changes only when a closed tool is opened. Dock/float moves therefore
    // retain the same logical analysis instance.
    [[nodiscard]] std::uint64_t instanceGeneration() const noexcept
    {
        return generation;
    }

  private:
    Presentation currentPresentation = Presentation::closed;
    std::uint64_t generation = 0;
};
