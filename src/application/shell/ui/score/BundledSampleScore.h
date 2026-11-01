#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>

// The score that ships inside the application, and the one place that knows
// where it is written to so the importer can read it.
//
// The importer reads files, and the import summary names the file it read, so
// the sample is written out once and opened like any other score rather than
// imported from memory through a second path. That also makes it a real file
// in a real folder, which is what the score library will list.
//
// JUCE core only, and everything but `installBundledSampleScore()` takes its
// folder and bytes as arguments, so the rule about when the file is rewritten
// is testable against a temporary directory.
namespace bundled_sample
{
// The name the sample is written under, and what the import summary shows.
[[nodiscard]] juce::String fileName();

// The menu item and button wording, naming the piece so it is clear this is
// someone's music and not a test pattern.
[[nodiscard]] juce::String menuLabel();

// The folder samples are written to: `Samples` under the application's own
// data folder. Not created by this call.
[[nodiscard]] juce::File defaultFolder();

// Write `size` bytes at `data` into `folder` as `fileName()`, unless a file
// with exactly those bytes is already there, and return it.
//
// Rewritten whenever the copy on disk is missing or differs -- a user may edit
// or truncate it, and a newer build may ship a newer sample -- so opening the
// sample always opens the one this build carries.
//
// Returns a default `juce::File` when the folder cannot be created or the file
// cannot be written. The caller has to say so: a sample that silently fails to
// open is a button that does nothing.
[[nodiscard]] juce::File install(const juce::File& folder, const void* data, std::size_t size);

// `install` into `defaultFolder()` with the bytes this build embeds.
[[nodiscard]] juce::File installBundledSampleScore();
} // namespace bundled_sample
