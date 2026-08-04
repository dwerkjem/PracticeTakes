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

TEST_CASE("every shipped tool allocates its single instance", "[tools][policy]")
{
    const auto isLive = [](const ToolInstanceId&) { return true; };

    for (const auto& definition : builtInToolCatalog().tools())
    {
        INFO("tool id: " << definition.id);
        const auto instance =
            ToolInstanceAllocator::nextInstanceId(builtInToolCatalog(), definition.id, isLive);
        REQUIRE(instance.has_value());
        // Even with everything live, a single-instance tool returns its one id.
        CHECK(instance->value() == definition.id);
    }
}
