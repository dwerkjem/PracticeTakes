#include "PartNames.h"

#include <array>
#include <cctype>
#include <string_view>
#include <utility>

namespace engraving
{
namespace
{
[[nodiscard]] std::string trimmed(const std::string& text)
{
    const auto isSpace = [](char character)
    { return std::isspace(static_cast<unsigned char>(character)) != 0; };

    std::size_t first = 0;
    std::size_t last = text.size();

    while (first < last && isSpace(text[first]))
    {
        ++first;
    }

    while (last > first && isSpace(text[last - 1]))
    {
        --last;
    }

    return text.substr(first, last - first);
}

[[nodiscard]] std::string lowered(const std::string& text)
{
    std::string result = text;

    for (char& character : result)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return result;
}

// The conventional short forms, keyed by the lower-cased name. Covers what the
// corpus and an ordinary song or chamber score carry; a name outside it falls
// back to its first letters, which is a reasonable guess rather than a wrong one.
constexpr std::array<std::pair<std::string_view, std::string_view>, 40> knownAbbreviations{{
    {"voice", "Vo."},       {"gesang", "Ges."},       {"singstimme", "Sgst."},
    {"soprano", "S."},      {"mezzo-soprano", "Mz."}, {"alto", "A."},
    {"contralto", "A."},    {"tenor", "T."},          {"baritone", "Bar."},
    {"bass", "B."},         {"treble", "Tr."},        {"counter", "C."},
    {"piano", "Pno."},      {"pianoforte", "Pf."},    {"klavier", "Kl."},
    {"organ", "Org."},      {"harpsichord", "Hpd."},  {"harp", "Hp."},
    {"guitar", "Gtr."},     {"violin", "Vln."},       {"violino", "Vl."},
    {"viola", "Vla."},      {"violoncello", "Vc."},   {"cello", "Vc."},
    {"double bass", "Cb."}, {"contrabass", "Cb."},    {"flute", "Fl."},
    {"oboe", "Ob."},        {"clarinet", "Cl."},      {"bassoon", "Bsn."},
    {"horn", "Hn."},        {"trumpet", "Tpt."},      {"trombone", "Tbn."},
    {"tuba", "Tba."},       {"timpani", "Timp."},     {"women", "W."},
    {"men", "M."},          {"choir", "Ch."},         {"chorus", "Ch."},
    {"recorder", "Rec."},
}};

[[nodiscard]] bool isNumberWord(const std::string& word)
{
    if (word.empty())
    {
        return false;
    }

    // Arabic or Roman: "Violin 2", "Horn II".
    const bool arabic = word.find_first_not_of("0123456789") == std::string::npos;
    const bool roman = word.find_first_not_of("IVX") == std::string::npos;

    return arabic || roman;
}
} // namespace

std::string displayedPartName(const std::string& name)
{
    return trimmed(name);
}

std::string abbreviatedPartName(const std::string& name, const std::string& abbreviation)
{
    if (std::string given = trimmed(abbreviation); !given.empty())
    {
        return given;
    }

    std::string full = trimmed(name);

    if (full.empty())
    {
        return {};
    }

    // A trailing number belongs to the abbreviation too: two violins have to
    // stay told apart on every system, not only the first.
    std::string stem = full;
    std::string number;

    if (const std::size_t space = full.find_last_of(' '); space != std::string::npos)
    {
        const std::string last = full.substr(space + 1);

        if (isNumberWord(last))
        {
            stem = trimmed(full.substr(0, space));
            number = " " + last;
        }
    }

    const std::string key = lowered(stem);

    for (const auto& [known, shortForm] : knownAbbreviations)
    {
        if (key == known)
        {
            return std::string(shortForm) + number;
        }
    }

    if (full.size() <= 4)
    {
        return full;
    }

    // The first three letters of the name, skipping anything that is not one:
    // "[CANTUS] [Falsetto]" shortens to "CAN.", not "[CA.".
    std::string letters;

    for (const char character : stem)
    {
        if (std::isalpha(static_cast<unsigned char>(character)) != 0)
        {
            letters.push_back(character);

            if (letters.size() == 3)
            {
                break;
            }
        }
    }

    if (letters.empty())
    {
        return full;
    }

    return letters + "." + number;
}
} // namespace engraving
