#include "TakeManifest.h"

#include <cmath>
#include <utility>

namespace
{
using take::TakeState;

// The states a saved take can be in, as the manifest spells them. Spelled
// rather than numbered so the file reads, and so reordering the enum cannot
// change what an old file means.
struct StateName
{
    TakeState state;
    const char* name;
};

constexpr StateName stateNames[] = {
    {TakeState::completed, "completed"},
    {TakeState::stoppedEarly, "stoppedEarly"},
    {TakeState::failed, "failed"},
};

struct KindName
{
    midi::MidiEventKind kind;
    const char* name;
};

constexpr KindName kindNames[] = {
    {midi::MidiEventKind::noteOn, "on"},
    {midi::MidiEventKind::noteOff, "off"},
    {midi::MidiEventKind::sustainPedal, "pedal"},
};

const char* nameOf(TakeState state)
{
    for (const auto& entry : stateNames)
    {
        if (entry.state == state)
        {
            return entry.name;
        }
    }
    return stateNames[0].name;
}

std::optional<TakeState> stateNamed(const juce::String& name)
{
    for (const auto& entry : stateNames)
    {
        if (name == entry.name)
        {
            return entry.state;
        }
    }
    return std::nullopt;
}

std::optional<midi::MidiEventKind> kindNamed(const juce::String& name)
{
    for (const auto& entry : kindNames)
    {
        if (name == entry.name)
        {
            return entry.kind;
        }
    }
    return std::nullopt;
}

const char* nameOf(midi::MidiEventKind kind)
{
    for (const auto& entry : kindNames)
    {
        if (entry.kind == kind)
        {
            return entry.name;
        }
    }
    return kindNames[0].name;
}

juce::var integer(std::int64_t value)
{
    return juce::var(static_cast<juce::int64>(value));
}

// A whole number from a field, or `fallback` when it is absent or not one.
std::int64_t integerOf(const juce::var& value, std::int64_t fallback)
{
    if (value.isInt() || value.isInt64())
    {
        return static_cast<std::int64_t>(static_cast<juce::int64>(value));
    }
    if (value.isDouble())
    {
        const double number = static_cast<double>(value);
        // A whole number written with a point, as some writers do.
        if (std::isfinite(number) && !(number > std::floor(number)))
        {
            return static_cast<std::int64_t>(number);
        }
    }
    return fallback;
}

double numberOf(const juce::var& value, double fallback)
{
    if (value.isInt() || value.isInt64() || value.isDouble())
    {
        const double number = static_cast<double>(value);
        return std::isfinite(number) ? number : fallback;
    }
    return fallback;
}

int smallIntegerOf(const juce::var& value, int fallback, int lowest, int highest)
{
    const std::int64_t number = integerOf(value, fallback);
    return number < lowest || number > highest ? fallback : static_cast<int>(number);
}

library::DecodedManifest refused(std::string_view error)
{
    return {std::nullopt, std::string(error)};
}
} // namespace

namespace library
{
std::string encode(const TakeManifest& manifest)
{
    auto* root = new juce::DynamicObject();
    root->setProperty(
        "formatVersion",
        juce::String(manifest.majorVersion) + "." + juce::String(manifest.minorVersion));
    root->setProperty("recorded", integer(manifest.recordedMs));
    root->setProperty("scoreTitle", juce::String(manifest.scoreTitle));
    root->setProperty("scoreFile", juce::String(manifest.scoreFile));
    root->setProperty("startTick", integer(manifest.startTick));
    root->setProperty("scalePercent", manifest.scalePercent);
    root->setProperty("sampleRate", manifest.sampleRate);
    root->setProperty("referenceHz", manifest.referenceHz);
    root->setProperty("inputLatencyFrames", manifest.inputLatencyFrames);
    root->setProperty("outputLatencyFrames", manifest.outputLatencyFrames);
    root->setProperty("usedMicrophone", manifest.usedMicrophone);
    root->setProperty("usedKeyboard", manifest.usedKeyboard);
    root->setProperty("audioStart", integer(manifest.audioStart));
    root->setProperty("countInFrames", integer(manifest.countInFrames));
    root->setProperty("lastFrame", integer(manifest.lastFrame));
    if (manifest.stoppedInBar.has_value())
    {
        root->setProperty("stoppedInBar", integer(*manifest.stoppedInBar));
    }
    root->setProperty("state", juce::String(nameOf(manifest.state)));
    if (!manifest.failureReason.empty())
    {
        root->setProperty("failureReason", juce::String(manifest.failureReason));
    }
    root->setProperty("loopStartTick", integer(manifest.loopStartTick));
    root->setProperty("loopEndTick", integer(manifest.loopEndTick));

    juce::Array<juce::var> keyboard;
    keyboard.ensureStorageAllocated(static_cast<int>(manifest.keyboard.size()));
    for (const take::KeyboardEvent& event : manifest.keyboard)
    {
        juce::Array<juce::var> row;
        row.add(integer(event.frame));
        row.add(juce::String(nameOf(event.event.kind)));
        row.add(event.event.channel);
        row.add(event.event.noteNumber);
        row.add(event.event.velocity);
        row.add(event.event.pedalPosition);
        keyboard.add(juce::var(row));
    }
    root->setProperty("keyboard", juce::var(keyboard));

    return juce::JSON::toString(
               juce::var(root),
               juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::multiLine))
        .toStdString();
}

DecodedManifest decode(const std::string& document)
{
    if (static_cast<std::int64_t>(document.size()) > maximumManifestBytes)
    {
        return refused(l10n::key("library.take.error.too-large"));
    }

    juce::var parsed;
    if (juce::JSON::parse(
            juce::String::fromUTF8(document.data(), static_cast<int>(document.size())), parsed)
            .failed() ||
        !parsed.isObject())
    {
        return refused(l10n::key("library.take.error.unreadable"));
    }

    TakeManifest manifest;

    // The version first: a newer major may mean anything below it differently.
    const juce::String version = parsed["formatVersion"].toString();
    if (!version.containsOnly("0123456789.") || !version.contains("."))
    {
        return refused(l10n::key("library.take.error.unreadable"));
    }
    manifest.majorVersion = version.upToFirstOccurrenceOf(".", false, false).getIntValue();
    manifest.minorVersion = version.fromFirstOccurrenceOf(".", false, false).getIntValue();
    if (manifest.majorVersion > manifestMajorVersion)
    {
        return refused(l10n::key("library.take.error.newer"));
    }
    if (manifest.majorVersion < 1)
    {
        return refused(l10n::key("library.take.error.unreadable"));
    }

    const auto state = stateNamed(parsed["state"].toString());
    const double sampleRate = numberOf(parsed["sampleRate"], 0.0);
    if (!state.has_value() || sampleRate <= 0.0)
    {
        return refused(l10n::key("library.take.error.unreadable"));
    }
    manifest.state = *state;
    manifest.sampleRate = sampleRate;

    manifest.recordedMs = integerOf(parsed["recorded"], 0);
    manifest.scoreTitle = parsed["scoreTitle"].toString().toStdString();
    // A name beside the manifest and nothing else: a path that climbs out of
    // the take's folder is not a score copy this reader will open.
    const juce::String scoreFile = parsed["scoreFile"].toString();
    if (!scoreFile.containsAnyOf("/\\:") && scoreFile != "." && scoreFile != "..")
    {
        manifest.scoreFile = scoreFile.toStdString();
    }
    manifest.startTick = integerOf(parsed["startTick"], 0);
    manifest.scalePercent = transport::clampScalePercent(smallIntegerOf(
        parsed["scalePercent"], transport::defaultScalePercent, transport::minimumScalePercent,
        transport::maximumScalePercent));
    manifest.referenceHz = numberOf(parsed["referenceHz"], 440.0);
    if (manifest.referenceHz <= 0.0)
    {
        manifest.referenceHz = 440.0;
    }
    manifest.inputLatencyFrames = smallIntegerOf(parsed["inputLatencyFrames"], 0, 0, 1 << 20);
    manifest.outputLatencyFrames = smallIntegerOf(parsed["outputLatencyFrames"], 0, 0, 1 << 20);
    manifest.usedMicrophone = static_cast<bool>(parsed["usedMicrophone"]);
    manifest.usedKeyboard = static_cast<bool>(parsed["usedKeyboard"]);
    manifest.audioStart = integerOf(parsed["audioStart"], 0);
    manifest.countInFrames = std::max<std::int64_t>(0, integerOf(parsed["countInFrames"], 0));
    manifest.lastFrame = std::max<std::int64_t>(0, integerOf(parsed["lastFrame"], 0));
    if (parsed.hasProperty("stoppedInBar"))
    {
        manifest.stoppedInBar = std::max<std::int64_t>(0, integerOf(parsed["stoppedInBar"], 0));
    }
    manifest.failureReason = parsed["failureReason"].toString().toStdString();
    manifest.loopStartTick = std::max<std::int64_t>(0, integerOf(parsed["loopStartTick"], 0));
    manifest.loopEndTick =
        std::max(manifest.loopStartTick, integerOf(parsed["loopEndTick"], manifest.loopStartTick));

    // A keyboard event that does not read is dropped rather than refusing the
    // take: the rest of it is still the player's.
    if (const auto* keyboard = parsed["keyboard"].getArray(); keyboard != nullptr)
    {
        manifest.keyboard.reserve(static_cast<std::size_t>(keyboard->size()));
        for (const juce::var& row : *keyboard)
        {
            const auto* fields = row.getArray();
            if (fields == nullptr || fields->size() < 6)
            {
                continue;
            }
            const auto kind = kindNamed((*fields)[1].toString());
            if (!kind.has_value())
            {
                continue;
            }
            take::KeyboardEvent event;
            event.frame = integerOf((*fields)[0], 0);
            event.event.kind = *kind;
            event.event.channel = smallIntegerOf((*fields)[2], 1, 1, 16);
            event.event.noteNumber = smallIntegerOf((*fields)[3], 0, 0, 127);
            event.event.velocity = smallIntegerOf((*fields)[4], 0, 0, 127);
            event.event.pedalPosition = smallIntegerOf((*fields)[5], 0, 0, 127);
            manifest.keyboard.push_back(event);
        }
    }

    return {std::move(manifest), {}};
}

TakeManifest manifestOf(const take::Take& recorded, std::int64_t recordedMs, std::string scoreFile)
{
    TakeManifest manifest;
    manifest.recordedMs = recordedMs;
    manifest.scoreTitle = recorded.scoreTitle;
    manifest.scoreFile = std::move(scoreFile);
    manifest.startTick = recorded.startTick;
    manifest.scalePercent = recorded.scalePercent;
    manifest.sampleRate = recorded.sampleRate;
    manifest.referenceHz = recorded.referenceHz;
    manifest.inputLatencyFrames = recorded.inputLatencyFrames;
    manifest.outputLatencyFrames = recorded.outputLatencyFrames;
    manifest.usedMicrophone = recorded.usedMicrophone;
    manifest.usedKeyboard = recorded.usedKeyboard;
    manifest.audioStart = recorded.audioStart;
    manifest.countInFrames = recorded.countInFrames;
    manifest.lastFrame = recorded.lastFrame;
    manifest.stoppedInBar = recorded.stoppedInBar;
    manifest.state = recorded.state();
    manifest.failureReason = recorded.lifecycle.failureReason();
    if (recorded.table != nullptr && recorded.table->loopEnabled)
    {
        manifest.loopStartTick = recorded.table->loopStartTick;
        manifest.loopEndTick = recorded.table->loopEndTick;
    }
    manifest.keyboard = recorded.keyboard;
    return manifest;
}

take::TakeLifecycle lifecycleEndingIn(take::TakeState state, std::string reason)
{
    take::TakeLifecycle lifecycle;
    static_cast<void>(lifecycle.apply(take::TakeEvent::performanceStarted));
    if (state == TakeState::completed)
    {
        static_cast<void>(lifecycle.apply(take::TakeEvent::reachedEnd));
    }
    else if (state == TakeState::failed)
    {
        static_cast<void>(lifecycle.apply(take::TakeEvent::failed, std::move(reason)));
    }
    else
    {
        static_cast<void>(lifecycle.apply(take::TakeEvent::stopped));
    }
    return lifecycle;
}

void applyManifest(const TakeManifest& manifest, take::Take& onto)
{
    onto.lifecycle = lifecycleEndingIn(manifest.state, manifest.failureReason);
    onto.scoreTitle = manifest.scoreTitle;
    onto.startTick = manifest.startTick;
    onto.scalePercent = manifest.scalePercent;
    onto.sampleRate = manifest.sampleRate;
    onto.referenceHz = manifest.referenceHz;
    onto.inputLatencyFrames = manifest.inputLatencyFrames;
    onto.outputLatencyFrames = manifest.outputLatencyFrames;
    onto.usedMicrophone = manifest.usedMicrophone;
    onto.usedKeyboard = manifest.usedKeyboard;
    onto.audioStart = manifest.audioStart;
    onto.countInFrames = manifest.countInFrames;
    onto.lastFrame = manifest.lastFrame;
    onto.stoppedInBar = manifest.stoppedInBar;
    onto.keyboard = manifest.keyboard;
}
} // namespace library
