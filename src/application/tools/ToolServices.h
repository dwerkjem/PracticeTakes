#pragma once

#include "../theme/ThemeType.h"

namespace score
{
class CurrentScore;
}

class AudioInputService;
class SharedPitchAnalysis;

// The shared services every tool is built against.
//
// This bundle is the only route a tool has to application-wide state: a
// src/features/* tool must never reach into the shell or into another tool. It
// is a struct rather than a widening parameter list precisely so that adding a
// service later -- a transport, once #29 brings in reference tempo -- is one
// field here instead of a signature change at every factory.
//
// Every reference is non-owning and outlives every tool instance. That is
// guaranteed structurally rather than by a runtime check: MainComponent
// declares its live tools after the services they borrow, so reverse-order
// member destruction tears every instance down first.
struct ToolServices
{
    AudioInputService& audio;
    SharedPitchAnalysis& pitchAnalysis;

    // The score the application currently has open, or an empty holder when
    // none is. `const` because opening a score is the shell's business: a tool
    // reads the current score and never decides what it is.
    //
    // Read on the message thread only, like everything else about a score --
    // score::CurrentScore says why there is no lock. A tool reads it on its own
    // timer and compares the pointer against the one it last rendered, the same
    // polling arrangement SharedPitchAnalysis uses, because a newly opened
    // score is the least urgent update in the application.
    const score::CurrentScore& currentScore;

    // The theme a tool should start in. Later changes arrive through
    // ToolComponent::setTheme rather than by re-reading this, because a change
    // has to trigger a repaint and not just update state.
    Theme initialTheme = Theme::light;
};
