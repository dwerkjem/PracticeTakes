#include "MusicXmlSubset.h"

namespace score::musicxml
{
const std::map<std::string, std::string>& unsupportedConstructs()
{
    // design.md § Supported subset for the MVP is the source of this list. If
    // something here becomes part of the model later, it moves out of here --
    // this table and that section are meant to say the same thing.
    static const std::map<std::string, std::string> constructs{
        {"slur", "Slurs are phrasing marks; this importer reads only sounding ties."},
        {"articulations", "Articulation marks are not imported."},
        {"ornaments", "Ornaments are not imported."},
        {"technical", "Technical marks such as fingering and bowing are not imported."},
        {"harmony", "Chord symbols are not imported."},
        {"figured-bass", "Figured bass is not imported."},
        {"unpitched", "Percussion and other unpitched notes are not imported."},
        {"multiple-rest", "Multi-measure rests are imported as ordinary rests."},
        {"print", "Page and system layout is the renderer's to decide, so it was ignored."},
        {"defaults", "Engraving defaults such as fonts and page size are not part of the score."},
        {"part-group", "Instrument bracketing is engraving data and is not imported."},
    };

    return constructs;
}

const std::unordered_set<std::string>& knownElements()
{
    static const std::unordered_set<std::string> known{
        // Structure and metadata
        "score-partwise",
        "part-list",
        "score-part",
        "part",
        "measure",
        "work",
        "work-title",
        "work-number",
        "movement-title",
        "movement-number",
        "identification",
        "creator",
        "encoding",
        "software",
        "encoding-date",
        "supports",
        "rights",
        "source",
        "credit",
        "credit-words",
        "credit-type",
        "part-name",
        "part-abbreviation",
        "part-name-display",
        "part-abbreviation-display",
        "display-text",
        "score-instrument",
        "instrument-name",
        "instrument-sound",
        "midi-device",
        "midi-instrument",
        "midi-channel",
        "midi-program",
        "midi-unpitched",
        "volume",
        "pan",
        // Attributes
        "attributes",
        "divisions",
        "key",
        "fifths",
        "mode",
        "cancel",
        "time",
        "beats",
        "beat-type",
        "senza-misura",
        "clef",
        "sign",
        "line",
        "clef-octave-change",
        "staves",
        "instruments",
        "transpose",
        "diatonic",
        "chromatic",
        "octave-change",
        "double",
        "staff-details",
        "measure-style",
        // Time and events
        "note",
        "backup",
        "forward",
        "duration",
        "chord",
        "grace",
        "cue",
        "rest",
        "pitch",
        "step",
        "alter",
        "octave",
        "voice",
        "staff",
        "type",
        "dot",
        "accidental",
        "stem",
        "beam",
        "tie",
        "notations",
        "tied",
        "time-modification",
        "actual-notes",
        "normal-notes",
        "normal-type",
        "normal-dot",
        "tuplet",
        "tuplet-actual",
        "tuplet-normal",
        "notehead",
        "instrument",
        "display-step",
        "display-octave",
        // Lyrics
        "lyric",
        "syllabic",
        "text",
        "extend",
        "elision",
        "humming",
        "laughing",
        "end-line",
        "end-paragraph",
        // Directions
        "direction",
        "direction-type",
        "dynamics",
        "words",
        "metronome",
        "beat-unit",
        "beat-unit-dot",
        "per-minute",
        "metronome-note",
        "sound",
        "offset",
        "wedge",
        "rehearsal",
        "segno",
        "coda",
        "dashes",
        "bracket",
        "octave-shift",
        "pedal",
        "damp",
        "damp-all",
        "eyeglasses",
        "string-mute",
        "scordatura",
        "image",
        "principal-voice",
        "accordion-registration",
        "percussion",
        "other-direction",
        // Barlines
        "barline",
        "bar-style",
        "repeat",
        "ending",
        "fermata",
        "wavy-line",
        "segno-barline",
    };

    return known;
}

bool isDynamicMarking(const std::string& name)
{
    static const std::unordered_set<std::string> markings{
        "p",    "pp",    "ppp",    "pppp", "ppppp", "pppppp", "f",    "ff",   "fff",
        "ffff", "fffff", "ffffff", "mp",   "mf",    "sf",     "sfp",  "sfpp", "fp",
        "rf",   "rfz",   "sfz",    "sffz", "fz",    "n",      "sfzp", "pf"};

    return markings.count(name) > 0;
}
} // namespace score::musicxml
