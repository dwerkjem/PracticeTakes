#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <string>
#include <string_view>

enum class SettingsTransferExportStatus
{
    exported,
    cancelled,
    ioError,
};

struct SettingsTransferExportResult
{
    SettingsTransferExportStatus status = SettingsTransferExportStatus::ioError;
    std::string error;
};

class SettingsTransferFile
{
  public:
    [[nodiscard]] static SettingsTransferExportResult
    exportDocument(std::string_view document, const juce::File& destination)
    {
        if (destination == juce::File{})
        {
            return {.status = SettingsTransferExportStatus::cancelled, .error = {}};
        }

        juce::TemporaryFile temporary(destination);
        std::unique_ptr<juce::FileOutputStream> stream(temporary.getFile().createOutputStream());
        if (stream == nullptr || !stream->openedOk() ||
            !stream->write(document.data(), document.size()))
        {
            return {
                .status = SettingsTransferExportStatus::ioError,
                .error = "The settings export could not be written"};
        }

        stream->flush();
        if (stream->getStatus().failed())
        {
            return {
                .status = SettingsTransferExportStatus::ioError,
                .error = "The settings export could not be flushed"};
        }
        stream.reset();
        if (!temporary.overwriteTargetFileWithTemporary())
        {
            return {
                .status = SettingsTransferExportStatus::ioError,
                .error = "The settings export could not replace the destination file"};
        }

        return {.status = SettingsTransferExportStatus::exported, .error = {}};
    }
};