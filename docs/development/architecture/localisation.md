# Localisation

How Practice Takes addresses the text it shows a user, and what it never
translates. The proposal and its reasoning are in
`openspec/changes/internationalization/`; this page is the working reference.

## Text is addressed by identifier

Every sentence a user sees is an entry in a catalogue under
`tools/packaging/localisation/`, one file per language (`en.txt`, `de.txt`),
embedded in the binary as `PracticeTakesCatalogues`. Code asks for it by a
stable identifier, never by its English:

```cpp
#include "application/localisation/Localisation.h"

label.setText(l10n::text("settings.appearance.theme"), juce::dontSendNotification);
l10n::format("score.where", {{"bar", juce::String(bar)}, {"part", partName}});
l10n::plural("score.import.bars", count);   // "score.import.bars.one" / ".other"
l10n::number(frequency, 1);                 // 440.0, or 440,0 in German
```

- **`text`**: the chosen language's entry, else English's, else the identifier
  itself — visibly wrong, and caught in CI before it ships.
- **`format`**: `{name}` placeholders filled by name, so a translation puts
  them in its own order. Never build a sentence by concatenation: word order
  is not shared between languages.
- **`plural`**: the form the language's plural rule picks (`.one`, `.other`,
  and `.few`/`.many` for a language that has them), with `{count}` filled in.
  Never test `count == 1`; French puts zero with one.
- **`number`**: the language's decimal separator and grouping. Units read the
  same everywhere (Hz, cents, BPM) are not translated.

An identifier kept in a table and looked up later is marked where it is
written, with `l10n::key("…")` or `l10n::pluralKey("…")`. They do nothing at
run time; they are how the check finds it.

A JUCE-free layer never composes a sentence. An error or reason it hands
upward is an identifier (`std::string(l10n::key("take.failed.score-changed"))`),
rendered where it is shown with `l10n::text`; a verdict with numbers in it stays
data, and `src/application/localisation/TakeText` (or the tool that shows it)
puts it into words.

The rules that are not text — plural categories, placeholder filling, number
formatting — are in the JUCE-free `LocalisationRules.h`.

## The check

`tools/scripts/quality/check_localisation.py` runs in the Python quality
check and fails when:

- code references an identifier `en.txt` does not define;
- `en.txt` defines one nothing in `src/` references (a reference in a comment
  or a test does not count);
- a translation defines one English does not, or a catalogue defines one twice;
- anything under `src/application/testcontrol` uses `l10n` at all.

A reference is a string literal as the first argument of a qualified
`l10n::text`/`format`/`plural`/`key`/`pluralKey` call. Pass a variable and
the check cannot see the key, so mark it with `l10n::key` where it is written.

## Checking a layout without a translator

`l10n::setLanguage("qps-ploc")`, or `--language qps-ploc` on the command line,
runs in a pseudolocale generated from English (`Pseudolocale.h`): every entry
accented, about 40% longer and bracketed. `test-suite capture --pseudolocale
--scratch` captures every surface in it. Read the pictures for two things:
text that is clipped, truncated or not wrapped, and text that is not bracketed,
which is text the catalogue does not hold. A layout sized for English should
grow by how much longer a language's text is than English's, so that English
lays out exactly as before -- the transport strip does this with
`l10n::englishText`, the Settings panels with their label column.

## What is never localised

Some text looks like interface text and is not. It stays a literal, in
English, whatever language the interface is in:

- **Approved-state names and descriptions** in `src/application/testcontrol`.
  The capture harness matches on them: translating one breaks `list-states`
  and every capture with it. Enforced by the check above.
- **Log and developer-facing text**: `DBG`, `juce::Logger`, assertion
  messages, test-control replies, command-line usage. Its reader is a
  developer reading a bug report, who needs the same words whatever the
  reporter's language.
- **The feedback service's operator messages** in `src/services/feedback-intake`,
  and the message the client sends it (`FeedbackComponent::previewText`: "Type:",
  "Title:", the context tag). Its reader is the maintainer, not the person who
  sent the feedback; the form around it is translated.
- **MusicXML element and attribute names.** `<slur>` is a tag, not a word; a
  diagnostic names it as written in the file, inside a translated sentence.
- **Settings keys**, and every other identifier written to disk: workspace and
  tool-settings property names, tool type identifiers, `.ptsettings` fields. A
  key is a storage identifier, and translating it would make a file
  unreadable by a copy of the application configured for another language.
- **Symbols standing alone**: ♯, ♭, ▲, ▼, ¢, −, the fraction glyphs. Inside a
  phrase, the phrase is the entry.
- **Musical naming.** "H" is not the German for "B" but a different naming
  system, chosen as its own preference; see `score::PitchNaming`.
