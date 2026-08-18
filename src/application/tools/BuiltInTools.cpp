#include "BuiltInTools.h"

#include "BuiltInToolCatalog.h"

#include "../../features/analysis/harmonics/HarmonicAnalyzerComponent.h"
#include "../../features/analysis/spectrogram/SpectrogramComponent.h"
#include "../../features/analysis/tuner/TunerComponent.h"

#include <memory>

const ToolRegistry& builtInToolRegistry()
{
    static const ToolRegistry registry(
        builtInToolCatalog(),
        {
            {"tuner", [](const ToolServices& services)
             { return std::make_unique<TunerComponent>(services.audio, services.pitchAnalysis); }},
            {"spectrogram", [](const ToolServices& services)
             { return std::make_unique<SpectrogramComponent>(services.audio); }},
            {"harmonic-analyzer",
             [](const ToolServices& services) {
                 return std::make_unique<HarmonicAnalyzerComponent>(
                     services.audio, services.pitchAnalysis);
             }},
        });

    // The catalog and the factory table are edited together; if they drift, a
    // tool either never opens or never appears. Fail loudly in development
    // rather than shipping a menu entry that does nothing.
    jassert(registry.isConsistent());
    return registry;
}
