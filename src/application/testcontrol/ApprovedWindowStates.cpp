#include "ApprovedWindowStates.h"

#include <algorithm>

namespace testcontrol
{
namespace
{
using Tools = std::vector<std::string>;

// Every approved state, in the order `list-states` reports them.
//
// Adding one here is what makes a surface verifiable; there is deliberately no
// way for the harness to compose a state of its own. Removing one removes it
// from manual verification, so this list is the reviewable record of what is
// checked before a release.
const std::vector<ApprovedWindowState>& states()
{
    static const std::vector<ApprovedWindowState> approved{
        // --- The shell on its own ---
        {"empty",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The shell with no tool open"},

        // --- One tool at a time ---
        {"tuner-docked", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner, docked in the workspace"},
        {"tuner-in-tune", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner reading a note dead in tune", 440.0, "graph"},
        {"tuner-sharp", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner reading a note sharp", 452.0, "graph"},
        {"tuner-flat", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner reading a note flat", 428.0, "graph"},
        {"tuner-bar", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner in its bar view, reading a note", 440.0, "bar"},
        {"tuner-meter", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner in its meter view, reading a note", 440.0, "meter"},
        {"tuner-advanced", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner with its advanced settings expanded", 440.0, "advanced"},
        {"spectrogram-tone", Tools{"spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The spectrogram with a steady tone in it", 440.0, ""},
        {"harmonics-tone", Tools{"harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The harmonic analyser reading a sustained pitch", 220.0, ""},
        {"all-tools-tone", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "Every audio tool at once, all analysing the same tone", 440.0, ""},
        {"tuner-floating", Tools{"tuner"}, ToolPresentation::floating, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner, in its own floating window"},
        {"spectrogram-docked", Tools{"spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The spectrogram, docked in the workspace"},
        {"spectrogram-floating", Tools{"spectrogram"}, ToolPresentation::floating,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The spectrogram, in its own floating window"},
        {"harmonics-docked", Tools{"harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The harmonic analyser, docked in the workspace"},
        {"harmonics-floating", Tools{"harmonics"}, ToolPresentation::floating,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The harmonic analyser, in its own floating window"},
        {"score-view-empty", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The score view with no score open, which is what a new session shows"},

        // --- Several tools at once. Nothing else reaches the split tree with
        // more than one leaf, or the tab strip at all.
        {"two-tools-split", Tools{"tuner", "spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "The tuner and spectrogram tiled side by side"},
        {"two-tools-tabbed", Tools{"tuner", "spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::tabbed, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The tuner and spectrogram sharing one tab strip"},
        {"tuner-harmonics-split", Tools{"tuner", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "The tuner and the harmonic analyser tiled side by side"},
        // Two copies of one tool: the only state where an instance past the
        // first is on screen (multi-instance-tools).
        {"two-tuners-split", Tools{"tuner", "tuner#2"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "Two tuners tiled side by side, both reading the same tone", 440.0, ""},
        {"three-tools-tabbed", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::tabbed, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "All three tools sharing one tab strip"},
        {"all-tools-docked",
         Tools{
             "tuner", "spectrogram", "harmonics", "score-view", "take-review", "note-view",
             "now-next", "input-level"},
         ToolPresentation::docked, WorkspaceArrangement::split, WindowGeometry::normal,
         MicrophoneCondition::available, false, "", false, "Every tool tiled at once"},

        // --- Window geometry ---
        {"narrow-window", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::narrow, MicrophoneCondition::available, false, "", false,
         "A window narrow enough that the title bar collapses to the hamburger menu"},
        {"fullscreen", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::fullscreen, MicrophoneCondition::available, false, "", false,
         "The application in fullscreen"},
        {"narrow-empty", Tools{}, ToolPresentation::none, WorkspaceArrangement::single,
         WindowGeometry::narrow, MicrophoneCondition::available, false, "", false,
         "The empty shell in a narrow window, with the collapsed menu and no tool"},

        // --- Audio conditions ---
        // Both carry a tone, which is what makes them pictures of *mute* rather
        // than of silence. Without one a capture instance has no input at all,
        // so the window says "no microphone detected" and the picture shows
        // disconnection wearing a mute surface's name. With one the tools have
        // a signal, mute stops it reaching them, and the interface says muted.
        {"microphone-muted", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::muted, false, "", false,
         "The tuner with the global microphone mute engaged", 440.0, ""},
        {"muted-all-tools", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::muted, false, "",
         false, "Every audio tool at once with the microphone muted", 440.0, ""},
        {"microphone-warning",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::unavailable,
         false,
         "",
         false,
         "The no-usable-input warning, which a working microphone otherwise hides"},

        // --- Settings and feedback ---
        {"settings-open",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "",
         false,
         "The settings window on its default panel"},
        {"settings-audio-device",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "audio-device",
         false,
         "The settings window showing audio device selection"},
        {"settings-appearance",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "appearance",
         false,
         "The settings window showing appearance and theme"},
        {"settings-midi",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "midi",
         false,
         "The settings window showing MIDI keyboard selection, with no keyboard connected"},
        {"settings-practice",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "practice",
         false,
         "The settings window showing practice presets, with the level meter beside Ignore "
         "sound below reading a tone",
         // A tone, so the meter beside the threshold has a level to show.
         440.0},
        {"settings-reset",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "reset",
         false,
         "The settings window showing reset and support"},
        {"feedback-over-tool", Tools{"tuner"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", true, "The feedback form opened over a tool that is already in use"},
        {"feedback-open",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         true,
         "The feedback form"},

        // --- An imported score ---
        //
        // The two states that name a fixture. Everything before this point is
        // reachable with a flag; the score import summary has nothing to show
        // until a file has been read, so these are the only states that ask
        // the shell to open a document on the way in.
        //
        // The trailing fields are positional: description, tone, tool view,
        // the fixture, and then whether to show the summary. The two empty
        // strings in the middle are the tone and the view, neither of which
        // means anything here.
        //
        // The two states below leave the summary showing, because the summary
        // is what they are pictures of. The two score-view states after them
        // suppress it, which is what lets a *tool* be photographed with a score
        // in it -- see `showScoreImportSummary` in ApprovedWindowStates.h, and
        // finding 7 in docs/development/architecture/score-model-findings.md,
        // which this is the resolution of.
        {"score-import-clean",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The score import summary for a file that imported with nothing dropped",
         0.0,
         "",
         "clean-import.musicxml"},
        {"score-import-diagnostics",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The score import summary for a file the importer had something to say about",
         0.0,
         "",
         "diagnostic-import.musicxml"},

        // --- The renderer, with a score in it ---
        //
        // The whole reason the suppression field exists. Everything about this
        // tool is visual, so an approved state that could only show it empty
        // would leave the feature unverifiable.
        {"score-view-notation", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view drawing a staff: key signature, accidentals, rests, dots, flags, "
         "beams, a tie, a clef change, a chord and lyrics",
         0.0, "", "notation-sample.musicxml", false},
        {"score-view-notation-large", Tools{"score-view"}, ToolPresentation::floating,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The same staff in a floating window, where it has the room to break into fewer "
         "systems",
         0.0, "", "notation-sample.musicxml", false},
        // A piano part, which used to show its right hand alone. Both staves,
        // braced, with the beats of the two hands lined up.
        {"score-view-grand-staff", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view drawing a piano part as a braced grand staff, both hands aligned", 0.0, "",
         "grand-staff-sample.musicxml", false},
        // The fixture the engraving-polish change is checked against: stacked
        // accidentals, seconds on both sides of a stem, a tie across a system
        // break, endings and a run of rests. Captured before any of it was
        // fixed, so each tier of that change has a picture to compare with.
        {"score-view-engraving", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view drawing the engraving fixture: chord accidentals, seconds, a tie "
         "across a system break, first and second endings and four bars of rest",
         0.0, "", "engraving-polish.musicxml", false},
        // The sample the application ships, opened the way the File menu and
        // the empty-window button open it: written out of the executable, then
        // imported. The one score state whose input is not a committed fixture
        // but the bytes a user actually gets.
        {"score-view-sample", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view showing the sample score the application ships: a Mahler song for "
         "voice and piano",
         0.0, "", bundledSampleFixtureName(), false},
        // The same song in each of the Score View's three views, so a picture of
        // the default says nothing it should not about the other two.
        {"score-view-page", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The score view in Page view: the sample score on its first page", 0.0,
         "page", bundledSampleFixtureName(), false},
        // The song with its voice hidden, from the Score View's Parts menu: the
        // piano alone, laid out as a piano score. The view is a settings
        // payload rather than a bare name, which is how a state reaches a
        // choice beyond the view.
        {"score-view-part-hidden", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view with the sample's voice hidden: the piano alone, in Page view", 0.0,
         "view=page;hidden=P1:Voice", bundledSampleFixtureName(), false},
        // The practice clock playing the sample, with the transport in the
        // control strip: the one state where the clock is running
        // (practice-clock).
        {"transport-playing", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The practice clock playing the sample score: the transport in the control strip, "
         "showing where it is",
         0.0, "", bundledSampleFixtureName(), false, false, true},
        // The score cursor at a known place: the clock moved to bar 6 and left
        // there, so the cursor, the sounding note and the two after it are
        // the same in every picture (score-cursor).
        {"score-view-cursor", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view with the practice clock at bar 6: the cursor, the note sounding and "
         "the next two ringed",
         0.0, "", bundledSampleFixtureName(), false, false, false, 5},
        // A take being recorded from the tone, with the transport saying so:
        // the one state where a take runs (takes).
        {"take-recording", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "A take being recorded: Finish in red, the readout counting the take, the tempo and "
         "loop locked",
         440.0, "", bundledSampleFixtureName(), false, false, false, -1, true},
        // A take under review (take-review): a pianist's take of the sample,
        // made up with known mistakes, marked on the score and listed beside it.
        {"take-review", Tools{"score-view", "take-review"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false,
         "A take under review: its notes marked on the score, and the list of results beside it",
         0.0, "", bundledSampleFixtureName(), false, false, false, -1, false, true},
        // A singer's take under review (sung-pitch-judging): the sample's
        // voice, sung with notes sharp, flat, wrong and uncertain.
        {"take-review-sung",
         Tools{"score-view", "take-review"},
         ToolPresentation::docked,
         WorkspaceArrangement::split,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "A sung take under review: in-tune notes ringed, sharp and flat ones with an arrow "
         "and their cents, and the list saying how far off each was",
         0.0,
         "",
         bundledSampleFixtureName(),
         false,
         false,
         false,
         -1,
         false,
         true,
         false,
         0.0,
         true},
        // The Mix popup over the sample (score-playback): playing the score,
        // with the piano's right hand muted.
        {"playback-mix",
         Tools{"score-view"},
         ToolPresentation::docked,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The Mix popup: Play the score on, the volume, and Mute and Solo for the voice and each "
         "hand of the piano, the right hand muted",
         440.0,
         "",
         bundledSampleFixtureName(),
         false,
         false,
         false,
         -1,
         false,
         false,
         true},
        // The tuner reading against the sample song's target (system-wide-
        // target-note): a tone a little flat of the voice's first note, at
        // A440, and the same at A442, where the same tone reads flatter.
        {"tuner-target", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner with a target: the sample song's first sung note, the tone 12 cents flat "
         "of it",
         334.0, "", bundledSampleFixtureName(), false},
        {"tuner-target-a442",
         Tools{"tuner"},
         ToolPresentation::docked,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The same at A442: the same tone reads further flat of the target",
         334.0,
         "",
         bundledSampleFixtureName(),
         false,
         false,
         false,
         -1,
         false,
         false,
         false,
         442.0},
        // The target picker open from the tuner (target-note-picker), over the
        // sample song so its key is there to step in.
        {"target-picker", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The target picker: a keyboard, a staff, a name, the note being sung, and steps", 334.0,
         "target-picker", bundledSampleFixtureName(), false},
        // The Note View (note-view): a tone a little under the sample song's
        // target, on the keyboard, the staff and the trail.
        {"note-view", Tools{"note-view"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The Note View: the note being sung lit on a keyboard and drawn on a staff, against "
         "the target, with its trail, and the acceptable range around the target coloured by "
         "how close the note is",
         // About 12 cents above the sample's target E4, the middle of the
         // default range. The tone drifts 45 cents either way on purpose, so
         // a picture catches it green, blue or amber.
         332.0, "", bundledSampleFixtureName(), false},
        // The Note View with nothing to show: no tone, no score.
        {"note-view-empty", Tools{"note-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The Note View before anything is sung or a score opened"},
        // Now & Next (now-and-next): the sample's voice, the clock resting at
        // bar 2, with the current note and the notes coming.
        {"now-next", Tools{"now-next"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "Now & Next: the note at the cursor large, with its lyric, staff and time, and the "
         "notes coming, with rests, repeats and breaths",
         0.0, "", bundledSampleFixtureName(), false, false, false, 1},
        {"now-next-contrast", Tools{"now-next"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "Now & Next in high contrast, for a music stand", 0.0, "high-contrast",
         bundledSampleFixtureName(), false, false, false, 1},
        // At the very start: the strip has a rest, then a breath mark before
        // the next phrase.
        {"now-next-start", Tools{"now-next"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "Now & Next at the start of the song: a rest in the strip and a breath mark before "
         "the next phrase",
         0.0, "", bundledSampleFixtureName(), false, false, false, 0},
        // The Input Level tool (#381): a tone well over the default threshold,
        // so the bar is past the line and the words say it counts.
        {"input-level", Tools{"input-level"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The Input Level tool reading a tone: the level, its peak, and the line at Ignore "
         "sound below",
         440.0, ""},
        // The Take Review tool before any take: what most sessions see first.
        {"take-review-empty", Tools{"take-review"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The Take Review tool with no take yet: how to record one"},
        {"score-view-vertical", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view in Vertical scroll: the sample score as one scrolling column", 0.0,
         "vertical", bundledSampleFixtureName(), false},
        {"score-view-horizontal", Tools{"score-view"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false,
         "The score view in Horizontal scroll: the sample score on one long system", 0.0,
         "horizontal", bundledSampleFixtureName(), false},
        // A score being imported: the window that says which file is being
        // read, held there. The shipped sample, so the name on it is the one a
        // new user sees when they open it.
        {"score-import-in-progress",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The score import window while a score is being read",
         0.0,
         "",
         bundledSampleFixtureName(),
         true,
         true},
    };

    return approved;
}

// The approved click targets. Each id matches the component id set on the
// corresponding object in the shell, so a rename that misses one is caught by
// the click failing rather than by silently clicking nothing.
const std::vector<ApprovedClickTarget>& targets()
{
    static const std::vector<ApprovedClickTarget> approved{
        {"file-button", "Opens the file menu"},
        {"tools-button", "Opens the tools menu"},
        {"settings-button", "Opens the settings menu"},
        {"help-button", "Opens the help menu"},
        {"microphone-button", "Toggles the global microphone mute"},
        {"open-sample-button", "Opens the sample score from the empty window"},
        {"hamburger-button", "Opens the collapsed menu in a narrow window"},
        {"fullscreen-button", "Toggles fullscreen"},
    };

    return approved;
}

template <typename Item>
[[nodiscard]] const Item* findById(const std::vector<Item>& items, const std::string& id)
{
    const auto match =
        std::find_if(items.begin(), items.end(), [&id](const Item& item) { return item.id == id; });

    return match == items.end() ? nullptr : &*match;
}
} // namespace

const std::vector<ApprovedWindowState>& approvedWindowStates()
{
    return states();
}

const std::vector<ApprovedClickTarget>& approvedClickTargets()
{
    return targets();
}

const ApprovedWindowState* findApprovedWindowState(const std::string& id)
{
    return findById(states(), id);
}

const ApprovedClickTarget* findApprovedClickTarget(const std::string& id)
{
    return findById(targets(), id);
}

const std::string& approvedScoreFixtureDirectory()
{
    // Committed, and deliberately not the generated corpus under
    // build/manual-score-import/: a reference image whose input is untracked
    // is not a reference image, and a capture that differs because someone
    // regenerated a fixture is a false failure.
    //
    // Its own directory rather than the real-score corpus beside it. That
    // corpus is real exporter output, pinned row by row in its own manifest
    // and licence-checked per file; these two are hand-written for a camera.
    static const std::string directory{"src/tests/resources/harness-scores"};

    return directory;
}

const std::string& bundledSampleFixtureName()
{
    // Not a file name, so it can never collide with one in the fixture
    // directory: a state naming it gets the shipped sample instead.
    static const std::string name{"<bundled sample>"};

    return name;
}

const std::vector<std::string>& approvedThemeNames()
{
    static const std::vector<std::string> names{"dark", "light"};

    return names;
}

const std::vector<std::string>& approvedGeometryNames()
{
    static const std::vector<std::string> names{
        "normal", "narrow", "tiny", "maximised", "fullscreen", "slim", "squat"};

    return names;
}
} // namespace testcontrol
