#pragma once

#include "../theme/ThemeType.h"

#include <functional>

namespace score
{
class CurrentScore;
}

namespace transport
{
class PracticeClock;
}

namespace take
{
class TakeRecorder;
}

namespace pitch
{
class ReferencePitch;
class CurrentTarget;
} // namespace pitch

class AudioInputService;
class MidiInputService;
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

    // The one practice clock: its position, the transport, the metronome. A
    // tool reads its position through a ClockListener and may drive it -- play,
    // seek, loop -- because practising a passage is a tool's business; it
    // never owns it.
    transport::PracticeClock& clock;

    // The takes recorded this session, and the one being recorded. A tool may
    // read them -- #52's review will -- and start or stop one, as the transport
    // does.
    take::TakeRecorder& takes;

    // The application's A4, which every pitch-to-frequency conversion uses
    // (openspec/changes/archive/2026-11-08-system-wide-target-note, decision
    // 1). Read on a tool's own timer; set in Settings, never by a tool.
    const pitch::ReferencePitch& referencePitch;

    // The note the player is aiming at, or why there is none
    // (system-wide-target-note, decision 2). Set by the shell from each score
    // opened, and by the player through a tool: the tuner's picker and the
    // Score View's menu (target-note-picker).
    pitch::CurrentTarget& target;

    // The MIDI keyboard. A tool that wants its keys registers a consumer of
    // its own, as the take recorder does (note-view design.md decision 4).
    MidiInputService& midi;

    // The theme a tool should start in. Later changes arrive through
    // ToolComponent::setTheme rather than by re-reading this, because a change
    // has to trigger a repaint and not just update state.
    Theme initialTheme = Theme::light;

    // Show the import summary of the score now open, the window Open Score
    // showed: what the Score View's "N import diagnostics" opens (#350). The
    // shell's window, so a tool asks for it rather than drawing its own. Empty
    // does nothing.
    std::function<void()> showScoreImportSummary;
};
