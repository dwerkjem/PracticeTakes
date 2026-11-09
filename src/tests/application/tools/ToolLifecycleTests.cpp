#include <catch2/catch_test_macros.hpp>

#include "application/tools/BuiltInToolCatalog.h"
#include "application/tools/ToolComponent.h"
#include "application/tools/ToolInstanceAllocator.h"

#include <set>
#include <string>
#include <vector>

namespace
{
// A tool that records what the shell does to it, in order. Nothing here needs a
// window: a juce::Component that is never added to the desktop is just an
// object, which is the point of testing the contract rather than the shell.
class RecordingTool final : public ToolComponent
{
  public:
    explicit RecordingTool(std::vector<std::string>& log) : events(log)
    {
        events.push_back("create");
    }

    ~RecordingTool() override
    {
        events.push_back("destroy");
    }

    void setTheme(Theme) override
    {
        events.push_back("setTheme");
    }

    void resetToDefaults() override
    {
        events.push_back("reset");
        stored.clear();
    }

    void textScaleChanged() override
    {
        events.push_back("textScale " + std::to_string(textScale()));
    }

    // What the tool would draw its body text at.
    [[nodiscard]] float bodySize() const noexcept
    {
        return textSize(appearance::TextRole::body);
    }

    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override
    {
        events.push_back("captureSettings");
        return ToolSettingsPayload{settingsVersion, stored};
    }

    void applySettings(const ToolSettingsPayload& payload) override
    {
        events.push_back("applySettings");
        // Mirrors what a real tool must do: refuse a payload it cannot read
        // rather than trusting the version.
        if (payload.version == settingsVersion)
        {
            stored = payload.data;
        }
    }

    [[nodiscard]] const std::string& storedSettings() const noexcept
    {
        return stored;
    }

    static constexpr int settingsVersion = 3;

  private:
    std::vector<std::string>& events;
    std::string stored;
};

// A tool that persists nothing, like the spectrogram and harmonic analyzer.
class StatelessTool final : public ToolComponent
{
  public:
    void setTheme(Theme) override {}
    void resetToDefaults() override {}
};

[[nodiscard]] ToolCatalog multiInstanceCatalog()
{
    return ToolCatalog({
        ToolDefinition{"solo", "Solo", {}, ToolInstancePolicy::single, std::nullopt, {100, 100}},
        ToolDefinition{"many", "Many", {}, ToolInstancePolicy::multi, std::nullopt, {100, 100}},
    });
}
} // namespace

TEST_CASE("a tool instance runs through its lifecycle in order", "[tools][lifecycle]")
{
    std::vector<std::string> events;
    {
        auto tool = std::make_unique<RecordingTool>(events);
        tool->setTheme(Theme::dark);
        tool->applySettings({RecordingTool::settingsVersion, "restored"});
        CHECK(tool->storedSettings() == "restored");

        // Closing captures before the component is destroyed -- once it is
        // gone its settings are unrecoverable.
        const auto captured = tool->captureSettings();
        REQUIRE(captured.has_value());
        CHECK(captured->data == "restored");
    }

    CHECK(
        events == std::vector<std::string>{
                      "create", "setTheme", "applySettings", "captureSettings", "destroy"});
}

TEST_CASE("a tool that persists nothing captures no payload", "[tools][lifecycle]")
{
    StatelessTool tool;

    CHECK_FALSE(tool.captureSettings().has_value());
    // Applying anyway is harmless, which is what lets the shell restore
    // uniformly without asking whether a tool persists.
    tool.applySettings({1, "ignored"});
    CHECK_FALSE(tool.captureSettings().has_value());
}

TEST_CASE("a settings payload from another version is discarded", "[tools][lifecycle]")
{
    std::vector<std::string> events;
    RecordingTool tool(events);

    tool.applySettings({RecordingTool::settingsVersion, "good"});
    REQUIRE(tool.storedSettings() == "good");

    tool.applySettings({RecordingTool::settingsVersion + 1, "from a newer build"});
    CHECK(tool.storedSettings() == "good");
}

TEST_CASE("reopening a single-instance tool reuses its one instance", "[tools][policy]")
{
    const auto catalog = multiInstanceCatalog();
    std::set<std::string> live;
    const auto isLive = [&live](const ToolInstanceId& id) { return live.count(id.value()) == 1; };

    const auto first = ToolInstanceAllocator::nextInstanceId(catalog, "solo", isLive);
    REQUIRE(first.has_value());
    CHECK(first->value() == "solo");
    live.insert(first->value());

    // Opening it again while it is live must land on the same instance, which
    // is what makes the shell focus it instead of creating a second.
    const auto again = ToolInstanceAllocator::nextInstanceId(catalog, "solo", isLive);
    REQUIRE(again.has_value());
    CHECK(again->value() == "solo");
}

TEST_CASE("a multi-instance tool allocates distinct instance ids", "[tools][policy]")
{
    // No shipped tool is multi-instance, so this fake is what keeps the branch
    // exercised rather than rotting until the day a tool needs it.
    const auto catalog = multiInstanceCatalog();
    std::set<std::string> live;
    const auto isLive = [&live](const ToolInstanceId& id) { return live.count(id.value()) == 1; };

    const auto first = ToolInstanceAllocator::nextInstanceId(catalog, "many", isLive);
    REQUIRE(first.has_value());
    CHECK(first->value() == "many");
    live.insert(first->value());

    const auto second = ToolInstanceAllocator::nextInstanceId(catalog, "many", isLive);
    REQUIRE(second.has_value());
    CHECK(second->value() == "many#2");
    CHECK(second->toolId() == "many");
    live.insert(second->value());

    const auto third = ToolInstanceAllocator::nextInstanceId(catalog, "many", isLive);
    REQUIRE(third.has_value());
    CHECK(third->value() == "many#3");

    // Closing the second frees its slot rather than counting upwards forever.
    live.erase("many#2");
    const auto reused = ToolInstanceAllocator::nextInstanceId(catalog, "many", isLive);
    REQUIRE(reused.has_value());
    CHECK(reused->value() == "many#2");
}

TEST_CASE("an unregistered tool allocates no instance", "[tools][policy]")
{
    const auto catalog = multiInstanceCatalog();
    const auto isLive = [](const ToolInstanceId&) { return false; };

    CHECK_FALSE(ToolInstanceAllocator::nextInstanceId(catalog, "no-such-tool", isLive).has_value());
}

TEST_CASE("every shipped tool allocates by its declared policy", "[tools][policy]")
{
    // Only the first instance of each tool is live.
    const auto isLive = [](const ToolInstanceId& id) { return id.ordinal().value_or(1) == 1; };

    for (const auto& definition : builtInToolCatalog().tools())
    {
        INFO("tool id: " << definition.id);
        const auto instance =
            ToolInstanceAllocator::nextInstanceId(builtInToolCatalog(), definition.id, isLive);
        REQUIRE(instance.has_value());
        if (definition.instancePolicy == ToolInstancePolicy::single)
        {
            // A single-instance tool always returns its one id.
            CHECK(instance->value() == definition.id);
        }
        else
        {
            // A multi-instance tool's next copy is its second.
            CHECK(instance->value() == definition.id + "#2");
        }
    }
}

TEST_CASE("the microphone tools are multi-instance and the score tools are not", "[tools][policy]")
{
    const auto& catalog = builtInToolCatalog();
    CHECK(catalog.find("tuner")->instancePolicy == ToolInstancePolicy::multi);
    CHECK(catalog.find("spectrogram")->instancePolicy == ToolInstancePolicy::multi);
    CHECK(catalog.find("harmonic-analyzer")->instancePolicy == ToolInstancePolicy::multi);
    CHECK(catalog.find("score-view")->instancePolicy == ToolInstancePolicy::single);
}

TEST_CASE(
    "open or focus targets the first live instance of a multi-instance tool",
    "[tools][policy]")
{
    const auto catalog = multiInstanceCatalog();
    std::set<std::string> live{"many#2", "many#3"};
    const auto isLive = [&live](const ToolInstanceId& id) { return live.count(id.value()) == 1; };

    // The first instance was closed, so the first *live* one is the second.
    const auto focused = ToolInstanceAllocator::instanceToFocus(catalog, "many", isLive, 3);
    REQUIRE(focused.has_value());
    CHECK(focused->value() == "many#2");

    // Choosing it again changes nothing: open or focus never mints an ordinal.
    const auto again = ToolInstanceAllocator::instanceToFocus(catalog, "many", isLive, 3);
    REQUIRE(again.has_value());
    CHECK(again->value() == "many#2");
}

TEST_CASE("open or focus opens the first instance when none is live", "[tools][policy]")
{
    const auto catalog = multiInstanceCatalog();
    const auto isLive = [](const ToolInstanceId&) { return false; };

    const auto focused = ToolInstanceAllocator::instanceToFocus(catalog, "many", isLive, 4);
    REQUIRE(focused.has_value());
    CHECK(focused->value() == "many");
    CHECK_FALSE(ToolInstanceAllocator::instanceToFocus(catalog, "no-such-tool", isLive, 4));
}

TEST_CASE("open another is refused for a single-instance tool", "[tools][policy]")
{
    const auto catalog = multiInstanceCatalog();
    using ToolInstanceAllocator::OpenAnother;
    CHECK(
        ToolInstanceAllocator::canOpenAnother(*catalog.find("solo"), 1, 16) ==
        OpenAnother::singleInstance);
    // A multi-instance tool that reads no samples needs no audio stream.
    CHECK(
        ToolInstanceAllocator::canOpenAnother(*catalog.find("many"), 1, 0) == OpenAnother::allowed);
}

TEST_CASE("open another stops at the per-tool limit", "[tools][policy]")
{
    const auto catalog = multiInstanceCatalog();
    using ToolInstanceAllocator::OpenAnother;
    const auto limit = ToolInstanceAllocator::maximumInstancesPerTool;
    CHECK(
        ToolInstanceAllocator::canOpenAnother(*catalog.find("many"), limit - 1, 16) ==
        OpenAnother::allowed);
    CHECK(
        ToolInstanceAllocator::canOpenAnother(*catalog.find("many"), limit, 16) ==
        OpenAnother::toolLimitReached);
}

TEST_CASE(
    "open another needs a free audio stream only for a tool that reads samples",
    "[tools][policy]")
{
    using ToolInstanceAllocator::OpenAnother;
    ToolDefinition reading{"ears", "Ears", {}, ToolInstancePolicy::multi, std::nullopt, {}, true};

    CHECK(ToolInstanceAllocator::canOpenAnother(reading, 1, 1) == OpenAnother::allowed);
    CHECK(ToolInstanceAllocator::canOpenAnother(reading, 1, 0) == OpenAnother::noAudioStreamFree);
    // The per-tool limit is reported first: freeing a stream would not help.
    CHECK(
        ToolInstanceAllocator::canOpenAnother(
            reading, ToolInstanceAllocator::maximumInstancesPerTool, 0) ==
        OpenAnother::toolLimitReached);
}

TEST_CASE("only the tools that run their own FFT read audio samples", "[tools][catalog]")
{
    const auto& catalog = builtInToolCatalog();
    // The tuner reads its pitch from SharedPitchAnalysis, so it listens for the
    // input's state only and takes no stream.
    CHECK_FALSE(catalog.find("tuner")->readsAudioSamples);
    CHECK(catalog.find("spectrogram")->readsAudioSamples);
    CHECK(catalog.find("harmonic-analyzer")->readsAudioSamples);
    CHECK_FALSE(catalog.find("score-view")->readsAudioSamples);
}

TEST_CASE("a tool hears the text size once per change, snapped", "[tools][text-size]")
{
    std::vector<std::string> log;
    RecordingTool tool(log);
    log.clear();

    CHECK(tool.textScale() == 100);
    CHECK(tool.bodySize() == 13.0f);

    tool.setTextScale(150);
    tool.setTextScale(150);
    tool.setTextScale(133);
    CHECK(log == std::vector<std::string>{"textScale 150", "textScale 130"});
    CHECK(tool.textScale() == 130);
    CHECK(tool.bodySize() == 17.0f); // 16.9
}
