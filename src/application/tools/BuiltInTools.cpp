#include "BuiltInTools.h"

#include "BuiltInToolCatalog.h"

#include "../../features/analysis/harmonics/HarmonicAnalyzerComponent.h"
#include "../../features/analysis/spectrogram/SpectrogramComponent.h"
#include "../../features/analysis/tuner/TunerComponent.h"
#include "../../features/review/TakeReviewComponent.h"
#include "../../features/score/ScoreViewComponent.h"

#include <memory>

const ToolRegistry& builtInToolRegistry()
{
    static const ToolRegistry registry(
        builtInToolCatalog(),
        {
            {"tuner",
             [](const ToolServices& services)
             {
                 return std::make_unique<TunerComponent>(
                     services.audio, services.pitchAnalysis, services.referencePitch,
                     services.target);
             }},
            {"spectrogram", [](const ToolServices& services)
             { return std::make_unique<SpectrogramComponent>(services.audio); }},
            {"harmonic-analyzer",
             [](const ToolServices& services) {
                 return std::make_unique<HarmonicAnalyzerComponent>(
                     services.audio, services.pitchAnalysis);
             }},
            {"score-view",
             [](const ToolServices& services)
             {
                 return std::make_unique<ScoreViewComponent>(
                     services.currentScore, services.clock, services.takes, services.target);
             }},
            {"take-review", [](const ToolServices& services)
             { return std::make_unique<TakeReviewComponent>(services.takes, services.clock); }},
        });

    // The catalog and the factory table are edited together; if they drift, a
    // tool either never opens or never appears. Fail loudly in development
    // rather than shipping a menu entry that does nothing.
    jassert(registry.isConsistent());
    return registry;
}
