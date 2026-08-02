#include "MusicXmlReadContext.h"

namespace score::musicxml
{
DiagnosticLocation locationOf(const Part& part)
{
    DiagnosticLocation location;
    location.partId = part.id;

    return location;
}

DiagnosticLocation locationOf(const Part& part, const Measure& measure)
{
    DiagnosticLocation location = locationOf(part);
    location.measureNumber = measure.printedNumber;

    return location;
}

DiagnosticLocation locationOf(const Part& part, const Measure& measure, int voice)
{
    DiagnosticLocation location = locationOf(part, measure);
    location.voice = voice;

    return location;
}
} // namespace score::musicxml
