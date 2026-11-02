#include "BundledSampleScore.h"

#include "PracticeTakesSamples.h"

namespace bundled_sample
{
juce::String fileName()
{
    return "Mahler - Die zwei blauen Augen.mxl";
}

juce::String menuLabel()
{
    return "Open sample score (Mahler, \"Die zwei blauen Augen\")";
}

juce::File defaultFolder()
{
    const juce::File applicationData =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);

#if JUCE_MAC
    // On macOS this location is ~/Library itself, where an application's own
    // folder belongs under Application Support rather than loose beside the
    // system's.
    return applicationData.getChildFile("Application Support")
        .getChildFile("PracticeTakes")
        .getChildFile("Samples");
#else
    // The same PracticeTakes folder the settings file lives in on Linux and
    // Windows, so everything the application writes is in one place.
    return applicationData.getChildFile("PracticeTakes").getChildFile("Samples");
#endif
}

juce::File install(const juce::File& folder, const void* data, std::size_t size)
{
    if (data == nullptr || size == 0)
    {
        return {};
    }

    if (!folder.isDirectory() && !folder.createDirectory().wasOk())
    {
        return {};
    }

    juce::File file = folder.getChildFile(fileName());
    const juce::MemoryBlock wanted(data, size);

    if (file.existsAsFile() && file.getSize() == static_cast<juce::int64>(size))
    {
        juce::MemoryBlock onDisk;

        if (file.loadFileAsData(onDisk) && onDisk == wanted)
        {
            return file;
        }
    }

    // replaceWithData writes to a temporary file and moves it into place, so a
    // write that fails half way leaves the old copy rather than a truncated one.
    if (!file.replaceWithData(data, size))
    {
        return {};
    }

    return file;
}

juce::File installBundledSampleScore()
{
    return install(
        defaultFolder(), PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl,
        static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize));
}
} // namespace bundled_sample
