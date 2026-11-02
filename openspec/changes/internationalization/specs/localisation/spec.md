## ADDED Requirements

### Requirement: User-facing text is addressed by a stable identifier
Every string the application shows a user SHALL be stored in a catalogue and
referenced by a stable identifier, not written as a literal at the point it is
drawn.

Keying on the English text instead — which is what JUCE's `TRANS` intends —
means rewording an English sentence silently orphans every translation of it,
and means two different senses of one word collapse into one entry. This domain
has that collision immediately: "bar" is a measure in the import report and a
display mode in the tuner, and they are different words in every target
language.

#### Scenario: A string is shown
- **WHEN** the application draws any user-facing text
- **THEN** that text came from the catalogue, addressed by its identifier

#### Scenario: English is reworded
- **WHEN** the English text behind an identifier is changed
- **THEN** existing translations for that identifier remain in use

#### Scenario: One word, two senses
- **WHEN** a word means different things in different places
- **THEN** each place has its own identifier, so each can be translated
  differently

### Requirement: English is a locale rather than a set of literals
The English text SHALL live in a catalogue of its own, and SHALL be the
fallback every other locale resolves through.

This is what makes the catalogue the answer to "what does this application say
to a user", rather than a grep that cannot distinguish a sentence from a format
string or an element name.

#### Scenario: A locale is missing a string
- **WHEN** the chosen locale has no entry for an identifier
- **THEN** the English entry is shown

#### Scenario: An unknown locale is requested
- **WHEN** the application is asked for a locale it does not have
- **THEN** it runs in English rather than failing to start

### Requirement: Every identifier used exists, and every one stored is used
The build SHALL fail when code references an identifier the English catalogue
does not define, and when the English catalogue defines one nothing references.

A missing entry otherwise reaches a user as a raw identifier where a sentence
should be. An unused entry otherwise accumulates, and is re-translated into
every language each time, for a screen that no longer exists.

#### Scenario: A reference with no entry
- **WHEN** code references an identifier absent from the English catalogue
- **THEN** the build fails

#### Scenario: An entry with no reference
- **WHEN** the English catalogue defines an identifier nothing references
- **THEN** the build fails

### Requirement: Translators can reorder placeholders and choose plural forms
A catalogue entry SHALL use named placeholders, and a quantity SHALL select its
form by plural category rather than by testing whether the count is one.

Word order is not shared between languages, so a sentence assembled by
concatenation cannot be translated at all — only re-concatenated in the wrong
order. Positional placeholders are reorderable but give the translator nothing
to reorder *by*.

#### Scenario: A placeholder is moved
- **WHEN** a locale's entry places its named placeholders in a different order
  from English
- **THEN** the rendered text follows that locale's order

#### Scenario: A quantity is shown
- **WHEN** text includes a count
- **THEN** the form shown is the one its locale's plural category selects for
  that count

#### Scenario: A language needs more categories later
- **WHEN** a language requiring additional plural categories is added
- **THEN** the call sites that show quantities do not change

### Requirement: Some text is deliberately never localised
Approved-state names and their descriptions, developer-facing log text, the
feedback service's operator messages, MusicXML element names, and settings keys
SHALL NOT be localised.

The approved-state vocabulary is a contract with the capture harness, which
matches on those strings: translating one breaks `list-states` and every
capture with it. An element name such as `slur` is a tag, not a word. A settings
key is a storage identifier, and translating it would make a settings file
unreadable by a differently-configured copy of the application.

#### Scenario: The harness asks what states exist
- **WHEN** the application is driven through the test-control channel in any
  locale
- **THEN** the state names and descriptions it reports are unchanged

#### Scenario: Settings are written in one locale and read in another
- **WHEN** settings saved under one locale are read under another
- **THEN** every value is found, because the keys did not change

### Requirement: Musical naming is chosen separately from interface language
The note-naming convention and the octave-numbering convention SHALL each be a
user preference in their own right, independent of the interface language.

"H" is not the German translation of "B": it is a different naming system, and
which system a musician reads is a fact about their training rather than about
the language they want the interface in. A German speaker may want an English
interface and still read H; a student whose teacher says *Do* may want English
and solfège.

#### Scenario: Naming and language differ
- **WHEN** a user selects one interface language and a naming convention
  belonging to another tradition
- **THEN** both are honoured

#### Scenario: The language changes
- **WHEN** the interface language is changed
- **THEN** the naming convention is unchanged

#### Scenario: A note is named
- **WHEN** the same sounding pitch is shown under different naming conventions
- **THEN** each is named by that convention, including B against H, and
  including the octave number that convention uses

### Requirement: One naming path serves every tool
A sounding pitch SHALL be named by one shared function, and its spelling SHALL
follow the score where there is one.

Two naming paths produce two answers for one note. The existing pair already
does: the tuner's table is sharp-only, so a piece in F minor has its A♭
announced as G♯ by the tuner and spelled A♭ by the score.

#### Scenario: A flat-key piece is being played
- **WHEN** a score spells a pitch as A flat and that pitch is being heard
- **THEN** the tuner names it A flat and not G sharp

#### Scenario: No score is open
- **WHEN** a pitch is heard with no score to spell it
- **THEN** it is named by the chosen convention, consistently with how a score
  would have spelled it in that key

### Requirement: Numbers are formatted for the reader's locale
Numbers shown to a user SHALL use their locale's decimal separator and
grouping.

A tuner's readout is the most closely-read number in the application, and
`440.0 Hz` is `440,0 Hz` to four of the five first target languages. Units that
are read identically across them — hertz, cents, beats per minute — are not
translated.

#### Scenario: A decimal is shown
- **WHEN** a number with a fractional part is shown in a locale whose decimal
  separator is a comma
- **THEN** it is shown with a comma

### Requirement: Verifying a translated build does not scale with languages
Capture verification SHALL use a generated pseudolocale rather than a capture
pass per language.

Forty surfaces across resolutions and palettes is already a large grid, and
reviewing it is the only thing that makes a capture worth taking. Five
languages multiply it beyond what anybody reviews, and most of what translation
breaks in an interface — truncation, clipping, text that will not wrap — is
caused by length rather than by meaning, which a generated locale reproduces
without a translator.

#### Scenario: The sweep runs
- **WHEN** the capture sweep runs
- **THEN** it captures in English, and in the pseudolocale, and not once per
  language

#### Scenario: A string was missed by the sweep
- **WHEN** a surface is captured in the pseudolocale
- **THEN** any text not drawn from the catalogue is visibly distinguishable
  from text that was

#### Scenario: Pixel comparison stays meaningful
- **WHEN** the golden-image validation runs
- **THEN** it compares English captures, since comparing translated text pixel
  for pixel reports differences that are not defects
