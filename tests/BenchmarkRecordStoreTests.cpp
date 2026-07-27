#include "features/performance/BenchmarkRecordStore.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
struct TemporaryDirectory
{
    TemporaryDirectory()
        : directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("practice-takes-records", {}, true))
    {
        REQUIRE(directory.createDirectory().wasOk());
    }

    ~TemporaryDirectory()
    {
        directory.deleteRecursively();
    }

    juce::File directory;
};
} // namespace

TEST_CASE("benchmark record store saves lists and loads one immutable file per run")
{
    TemporaryDirectory temporary;
    performance::BenchmarkRecordStore store(temporary.directory);
    performance::BenchmarkRunRecord record;
    record.runId = "run-1";
    record.status = performance::RunStatus::cancelled;
    record.statusDetail = "cancelled after one trial";
    record.provenance.instrumentationOverheadStatus =
        performance::InstrumentationOverheadStatus::documented;

    REQUIRE(store.save(record) == performance::RecordStoreStatus::succeeded);
    REQUIRE(store.save(record) == performance::RecordStoreStatus::alreadyExists);
    REQUIRE(store.listRunIds() == std::vector<std::string>{"run-1"});

    const auto loaded = store.load("run-1");
    REQUIRE(loaded.status == performance::RecordStoreStatus::succeeded);
    REQUIRE(loaded.record->status == performance::RunStatus::cancelled);
    REQUIRE(loaded.record->statusDetail == record.statusDetail);
}

TEST_CASE("benchmark record store preserves corrupt and newer schema errors")
{
    TemporaryDirectory temporary;
    performance::BenchmarkRecordStore store(temporary.directory);
    REQUIRE(temporary.directory.getChildFile("corrupt.json").replaceWithText("not json"));

    auto newer = performance::BenchmarkRecordCodec::encode({});
    newer.getDynamicObject()->setProperty(
        "schemaVersion", static_cast<int>(performance::benchmarkRecordSchemaVersion + 1));
    REQUIRE(temporary.directory.getChildFile("newer.json")
                .replaceWithText(juce::JSON::toString(newer)));

    REQUIRE(store.load("corrupt").status == performance::RecordStoreStatus::invalid);
    REQUIRE(store.load("newer").status == performance::RecordStoreStatus::newerSchema);
    REQUIRE(store.load("missing").status == performance::RecordStoreStatus::notFound);
}

TEST_CASE("benchmark export atomically writes complete codec JSON for incomplete runs")
{
    TemporaryDirectory temporary;
    const auto destination = temporary.directory.getChildFile("export.json");
    performance::BenchmarkRunRecord record;
    record.runId = "incomplete";
    record.status = performance::RunStatus::failed;
    record.statusDetail = "correctness failure";
    record.trials = {{1}};
    record.provenance.instrumentationOverheadStatus =
        performance::InstrumentationOverheadStatus::measured;

    REQUIRE(
        performance::exportBenchmarkRecord(record, destination) ==
        performance::RecordStoreStatus::succeeded);
    const auto decoded = performance::BenchmarkRecordCodec::decode(
        juce::JSON::parse(destination.loadFileAsString()));

    REQUIRE(decoded.status == performance::RecordDecodeStatus::loaded);
    REQUIRE(decoded.record->schemaVersion == performance::benchmarkRecordSchemaVersion);
    REQUIRE(decoded.record->status == performance::RunStatus::failed);
    REQUIRE(decoded.record->trials.size() == 1);
    REQUIRE(
        decoded.record->provenance.instrumentationOverheadStatus ==
        performance::InstrumentationOverheadStatus::measured);
}