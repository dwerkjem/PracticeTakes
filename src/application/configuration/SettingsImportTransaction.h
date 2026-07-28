#pragma once

#include "SettingsTransferCodec.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <string>

enum class SettingsImportStatus
{
    cancelled,
    imported,
    applyFailed,
    persistenceFailed,
    rollbackFailed,
};

enum class SettingsImportStageStatus
{
    ready,
    cancelled,
    ioError,
    invalid,
    newerSchema,
    tooLarge,
};

struct SettingsImportStageResult
{
    SettingsImportStageStatus status = SettingsImportStageStatus::invalid;
    std::optional<SettingsTransferModel> model;
    std::string error;
};

struct SettingsImportResult
{
    SettingsImportStatus status = SettingsImportStatus::cancelled;
    std::string error;
};

class SettingsImportTransaction
{
  public:
    struct Callbacks
    {
        std::function<SettingsTransferModel()> capture;
        std::function<bool(const SettingsTransferModel&)> apply;
        std::function<bool(const SettingsTransferModel&)> persist;
    };

    [[nodiscard]] static SettingsImportResult execute(
        const SettingsTransferModel& candidate,
        bool confirmed,
        bool& automaticSaveEnabled,
        const Callbacks& callbacks)
    {
        if (!confirmed)
        {
            return {.status = SettingsImportStatus::cancelled, .error = {}};
        }
        if (!callbacks.capture || !callbacks.apply || !callbacks.persist)
        {
            return {
                .status = SettingsImportStatus::applyFailed,
                .error = "Settings import is not available"};
        }

        const ScopedSaveSuppression saveSuppression(automaticSaveEnabled);
        const auto prior = callbacks.capture();
        if (!callbacks.apply(candidate))
        {
            if (!callbacks.apply(prior))
            {
                return {
                    .status = SettingsImportStatus::rollbackFailed,
                    .error = "Settings import failed and the previous live settings could not be "
                             "restored"};
            }
            return {
                .status = SettingsImportStatus::applyFailed,
                .error = "The imported settings could not be applied"};
        }
        if (!callbacks.persist(candidate))
        {
            if (!callbacks.apply(prior))
            {
                return {
                    .status = SettingsImportStatus::rollbackFailed,
                    .error = "Settings import could not be saved and the previous live settings "
                             "could not be restored"};
            }
            return {
                .status = SettingsImportStatus::persistenceFailed,
                .error = "The imported settings could not be saved"};
        }

        return {.status = SettingsImportStatus::imported, .error = {}};
    }

    [[nodiscard]] static SettingsImportStageResult stage(
        const juce::File& source,
        const std::vector<WorkspaceBounds>& displayAreas,
        const WorkspaceToolRegistry& registry = WorkspaceToolRegistry())
    {
        if (source == juce::File{})
        {
            return {.status = SettingsImportStageStatus::cancelled, .model = {}, .error = {}};
        }

        auto stream = source.createInputStream();
        if (stream == nullptr || !stream->openedOk())
        {
            return {
                .status = SettingsImportStageStatus::ioError,
                .model = {},
                .error = "The settings file could not be read"};
        }

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock(
            data, static_cast<ssize_t>(SettingsTransferCodec::maximumDocumentBytes + 1));
        if (data.getSize() > SettingsTransferCodec::maximumDocumentBytes)
        {
            return {
                .status = SettingsImportStageStatus::tooLarge,
                .model = {},
                .error = "The settings file is too large"};
        }

        const auto decoded = SettingsTransferCodec::decode(
            {static_cast<const char*>(data.getData()), data.getSize()}, displayAreas, registry);
        if (decoded.status == SettingsTransferDecodeStatus::loaded && decoded.model.has_value())
        {
            return {
                .status = SettingsImportStageStatus::ready, .model = decoded.model, .error = {}};
        }
        if (decoded.status == SettingsTransferDecodeStatus::newerSchema)
        {
            return {
                .status = SettingsImportStageStatus::newerSchema,
                .model = {},
                .error = decoded.error};
        }
        return {
            .status = decoded.status == SettingsTransferDecodeStatus::tooLarge
                          ? SettingsImportStageStatus::tooLarge
                          : SettingsImportStageStatus::invalid,
            .model = {},
            .error = decoded.error};
    }

  private:
    class ScopedSaveSuppression
    {
      public:
        explicit ScopedSaveSuppression(bool& enabled) noexcept : flag(enabled), prior(enabled)
        {
            flag = false;
        }

        ~ScopedSaveSuppression()
        {
            flag = prior;
        }

      private:
        bool& flag;
        bool prior;
    };
};