#include "MusicXmlValueParsing.h"

#include <cctype>
#include <cmath>
#include <exception>

namespace score::musicxml
{
namespace
{
// Everything consumed apart from trailing whitespace? Both parsers below share
// this rule, and it is the rule that makes "3x" a rejection rather than a 3.
bool consumedEverything(const std::string& text, std::size_t consumed)
{
    while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed])) != 0)
    {
        ++consumed;
    }

    return consumed == text.size();
}
} // namespace

std::optional<long long> parseInteger(const std::string& text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    try
    {
        std::size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);

        return consumedEverything(text, consumed) ? std::optional<long long>{value} : std::nullopt;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::optional<double> parseDecimal(const std::string& text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    try
    {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);

        if (!consumedEverything(text, consumed) || !std::isfinite(value))
        {
            return std::nullopt;
        }

        return value;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::optional<Step> parseStep(const std::string& text)
{
    if (text.size() != 1)
    {
        return std::nullopt;
    }

    switch (std::toupper(static_cast<unsigned char>(text[0])))
    {
    case 'C':
        return Step::c;
    case 'D':
        return Step::d;
    case 'E':
        return Step::e;
    case 'F':
        return Step::f;
    case 'G':
        return Step::g;
    case 'A':
        return Step::a;
    case 'B':
        return Step::b;
    default:
        return std::nullopt;
    }
}

SyllabicPosition parseSyllabic(const std::string& text)
{
    if (text == "begin")
    {
        return SyllabicPosition::begin;
    }

    if (text == "middle")
    {
        return SyllabicPosition::middle;
    }

    if (text == "end")
    {
        return SyllabicPosition::end;
    }

    return SyllabicPosition::single;
}

double quarterNotesPerBeatUnit(const std::string& beatUnit, int dots)
{
    double quarters = 1.0;

    if (beatUnit == "whole")
    {
        quarters = 4.0;
    }
    else if (beatUnit == "half")
    {
        quarters = 2.0;
    }
    else if (beatUnit == "eighth")
    {
        quarters = 0.5;
    }
    else if (beatUnit == "16th")
    {
        quarters = 0.25;
    }
    else if (beatUnit == "32nd")
    {
        quarters = 0.125;
    }

    double scale = 1.0;
    double increment = 0.5;

    for (int dot = 0; dot < dots; ++dot)
    {
        scale += increment;
        increment /= 2.0;
    }

    return quarters * scale;
}
} // namespace score::musicxml
