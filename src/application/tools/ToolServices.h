#pragma once

#include "../theme/ThemeType.h"

class AudioInputService;

// The shared services every tool is built against.
//
// This bundle is the only route a tool has to application-wide state: a
// src/features/* tool must never reach into the shell or into another tool. It
// is a struct rather than a widening parameter list precisely so that adding a
// service later -- a transport, once #29 brings in reference tempo -- is one
// field here instead of a signature change at every factory.
//
// The audio reference is non-owning and outlives every tool instance. That is
// guaranteed structurally rather than by a runtime check: MainComponent
// declares its live tools after the services they borrow, so reverse-order
// member destruction tears every instance down first.
struct ToolServices
{
    AudioInputService& audio;

    // The theme a tool should start in. Later changes arrive through
    // ToolComponent::setTheme rather than by re-reading this, because a change
    // has to trigger a repaint and not just update state.
    Theme initialTheme = Theme::light;
};
