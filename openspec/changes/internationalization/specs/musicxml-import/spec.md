## MODIFIED Requirements

### Requirement: Unsupported content is reported without failing the import
Content outside the supported subset SHALL NOT cause a failure. Recognised but
unsupported constructs SHALL be dropped and reported as diagnostics.
Unrecognised elements SHALL be reported in aggregate by element name rather than
once per occurrence, so a file rich in unsupported markup does not produce an
unusable volume of diagnostics.

A diagnostic SHALL carry a stable identifier and typed parameters rather than a
rendered sentence, and SHALL be deduplicated on those rather than on text.

The importer is JUCE-free, and composing a sentence there puts user-facing prose
below the layer that knows what language the user reads. Deduplicating on that
prose also makes "the same finding" depend on wording: rewording a message
changes what merges with what, silently. A code and its parameters say the same
thing without either problem, and let a test assert that a slur was unsupported
in bar 1 rather than that a particular English sentence was produced.

#### Scenario: A score contains unsupported notations
- **WHEN** a score contains articulations, ornaments, or chord symbols that the
  subset excludes
- **THEN** the import succeeds, the score contains the supported content, and a
  diagnostic reports what was dropped

#### Scenario: An export contains many unrecognised elements
- **WHEN** a file contains hundreds of occurrences of the same unrecognised
  element
- **THEN** the diagnostics contain a single summarised entry for that element
  name, including how many occurrences were seen

#### Scenario: A diagnostic is reported as data
- **WHEN** the importer reports any diagnostic
- **THEN** it carries an identifier and parameters that name what happened and
  where, and no rendered sentence

#### Scenario: Rewording does not change what merges
- **WHEN** the text shown for a diagnostic identifier is changed
- **THEN** which diagnostics are treated as the same finding is unchanged
