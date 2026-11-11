#include <catch2/catch_test_macros.hpp>

#include "platform/library/TakeManifest.h"

// A saved take's manifest: every field back as it went in, versions read or
// refused, and a document that does not make sense refused rather than
// half-read.
namespace
{
take::Take endedTake()
{
    take::Take recorded;
    static_cast<void>(recorded.lifecycle.apply(take::TakeEvent::performanceStarted));
    static_cast<void>(recorded.lifecycle.apply(take::TakeEvent::stopped));
    recorded.scoreTitle = "Die zwei blauen Augen";
    recorded.startTick = 960;
    recorded.scalePercent = 75;
    recorded.sampleRate = 48000.0;
    recorded.referenceHz = 442.0;
    recorded.inputLatencyFrames = 128;
    recorded.outputLatencyFrames = 256;
    recorded.usedMicrophone = true;
    recorded.usedKeyboard = true;
    recorded.audioStart = -96000;
    recorded.countInFrames = 96000;
    recorded.lastFrame = 4'000'000'000LL;
    recorded.stoppedInBar = 7;
    take::KeyboardEvent press;
    press.frame = 1200;
    press.event.kind = midi::MidiEventKind::noteOn;
    press.event.channel = 3;
    press.event.noteNumber = 64;
    press.event.velocity = 90;
    take::KeyboardEvent pedal;
    pedal.frame = 1300;
    pedal.event.kind = midi::MidiEventKind::sustainPedal;
    pedal.event.pedalPosition = 127;
    recorded.keyboard = {press, pedal};
    return recorded;
}
} // namespace

TEST_CASE("a manifest reads back as it was written", "[library][take-manifest]")
{
    const take::Take original = endedTake();
    const library::TakeManifest written =
        library::manifestOf(original, 1'789'000'000'000LL, "score.mxl");
    const library::DecodedManifest read = library::decode(library::encode(written));
    REQUIRE(read.manifest.has_value());
    const library::TakeManifest& manifest = *read.manifest;

    CHECK(manifest.majorVersion == library::manifestMajorVersion);
    CHECK(manifest.recordedMs == 1'789'000'000'000LL);
    CHECK(manifest.scoreTitle == "Die zwei blauen Augen");
    CHECK(manifest.scoreFile == "score.mxl");
    CHECK(manifest.startTick == 960);
    CHECK(manifest.scalePercent == 75);
    CHECK(manifest.sampleRate == 48000.0);
    CHECK(manifest.referenceHz == 442.0);
    CHECK(manifest.inputLatencyFrames == 128);
    CHECK(manifest.outputLatencyFrames == 256);
    CHECK(manifest.usedMicrophone);
    CHECK(manifest.usedKeyboard);
    CHECK(manifest.audioStart == -96000);
    CHECK(manifest.countInFrames == 96000);
    CHECK(manifest.lastFrame == 4'000'000'000LL);
    CHECK(manifest.stoppedInBar == std::optional<std::int64_t>{7});
    CHECK(manifest.state == take::TakeState::stoppedEarly);
    REQUIRE(manifest.keyboard.size() == 2);
    CHECK(manifest.keyboard[0].frame == 1200);
    CHECK(manifest.keyboard[0].event.kind == midi::MidiEventKind::noteOn);
    CHECK(manifest.keyboard[0].event.channel == 3);
    CHECK(manifest.keyboard[0].event.noteNumber == 64);
    CHECK(manifest.keyboard[0].event.velocity == 90);
    CHECK(manifest.keyboard[1].event.kind == midi::MidiEventKind::sustainPedal);
    CHECK(manifest.keyboard[1].event.pedalPosition == 127);

    // And onto a take: the same take, but for its score, table and audio.
    take::Take loaded;
    library::applyManifest(manifest, loaded);
    CHECK(loaded.state() == take::TakeState::stoppedEarly);
    CHECK(loaded.scalePercent == 75);
    CHECK(loaded.lastFrame == original.lastFrame);
    CHECK(loaded.keyboard.size() == 2);
    CHECK(loaded.stoppedInBar == original.stoppedInBar);
}

TEST_CASE("a loaded take ends as the saved one did", "[library][take-manifest]")
{
    CHECK(
        library::lifecycleEndingIn(take::TakeState::completed, {}).state() ==
        take::TakeState::completed);
    CHECK(
        library::lifecycleEndingIn(take::TakeState::stoppedEarly, {}).state() ==
        take::TakeState::stoppedEarly);
    const auto failed = library::lifecycleEndingIn(take::TakeState::failed, "device lost");
    CHECK(failed.state() == take::TakeState::failed);
    CHECK(failed.failureReason() == "device lost");
    CHECK(library::lifecycleEndingIn(take::TakeState::completed, {}).hasPerformed());
}

TEST_CASE(
    "a manifest from a newer application is refused, an older one read",
    "[library][take-manifest]")
{
    CHECK(
        library::decode(R"({"formatVersion": "2.0", "state": "completed", "sampleRate": 44100})")
            .error == "library.take.error.newer");
    // A newer minor of this major adds fields this reader ignores.
    const auto minor = library::decode(
        R"({"formatVersion": "1.7", "state": "completed", "sampleRate": 44100, "future": true})");
    REQUIRE(minor.manifest.has_value());
    CHECK(minor.manifest->minorVersion == 7);
}

TEST_CASE("a manifest that makes no sense is refused", "[library][take-manifest]")
{
    CHECK(library::decode("not json").error == "library.take.error.unreadable");
    CHECK(library::decode("[1, 2]").error == "library.take.error.unreadable");
    CHECK(
        library::decode(R"({"state": "completed", "sampleRate": 44100})").error ==
        "library.take.error.unreadable");
    // A take that was cancelled, or is still going, was never saved.
    CHECK(
        library::decode(R"({"formatVersion": "1.0", "state": "cancelled", "sampleRate": 44100})")
            .error == "library.take.error.unreadable");
    CHECK(
        library::decode(R"({"formatVersion": "1.0", "state": "completed", "sampleRate": 0})")
            .error == "library.take.error.unreadable");
    CHECK(
        library::decode(
            std::string(static_cast<std::size_t>(library::maximumManifestBytes) + 1, ' '))
            .error == "library.take.error.too-large");
}

TEST_CASE("a manifest's doubtful fields fall back rather than refuse", "[library][take-manifest]")
{
    const auto read = library::decode(R"({
        "formatVersion": "1.0", "state": "completed", "sampleRate": 44100,
        "scalePercent": 9000, "referenceHz": -3, "scoreFile": "../../etc/passwd",
        "loopStartTick": 500, "loopEndTick": 100,
        "keyboard": [[10, "on", 99, 300, 64, 0], [20, "sideways", 1, 60, 64, 0], "junk"]
    })");
    REQUIRE(read.manifest.has_value());
    const auto& manifest = *read.manifest;
    CHECK(manifest.scalePercent == transport::defaultScalePercent);
    CHECK(manifest.referenceHz == 440.0);
    // A score copy is a name beside the manifest, never a path out of it.
    CHECK(manifest.scoreFile.empty());
    CHECK(manifest.loopEndTick == manifest.loopStartTick);
    // The event that reads is kept, its channel and note brought into range;
    // the ones that do not are dropped.
    REQUIRE(manifest.keyboard.size() == 1);
    CHECK(manifest.keyboard[0].event.channel == 1);
    CHECK(manifest.keyboard[0].event.noteNumber == 0);
}
