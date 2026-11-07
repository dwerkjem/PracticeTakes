#pragma once

#include "ApprovedWindowStates.h"

#include <string>

// The approved vocabulary, written out for the capture harness to read.
//
// The harness used to restate, beside each state's name, what the state shows:
// which tools it opens and how, which window it is a picture of, whether it is
// about its window size. Nothing kept the two lists equal, and they drifted --
// `all-tools-docked` opened five tools while the harness filed it under four.
// So the harness now restates nothing. This writes everything it needs to know
// about each state into one generated file, and the harness reads that.
//
// The file is committed, so the harness and its tests read it without a C++
// build, and ApprovedStatesManifestTests fails when it is stale. Regenerate it
// with `cmake --build build --target approved-states-manifest`.
//
// JUCE-free: plain JSON, written by hand so that the bytes are the same on every
// platform and every run.
namespace testcontrol
{
// Where the generated manifest lives, relative to the repository root.
[[nodiscard]] const std::string& approvedStatesManifestPath();

// The window a state is a picture of, by title: the Settings window, the
// feedback form, the score import window, a floating tool's own window. Empty
// for the main window.
[[nodiscard]] std::string windowTitleOf(const ApprovedWindowState& state);

// The whole manifest, as the committed file should read.
[[nodiscard]] std::string approvedStatesManifestJson();
} // namespace testcontrol
