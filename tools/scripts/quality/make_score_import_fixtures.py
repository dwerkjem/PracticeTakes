#!/usr/bin/env python3
"""Write the files a tester opens to verify score import by hand.

Tasks 5.1-5.4 of the `score-import-command` change cannot be automated: the
file chooser is a native dialog the test-control channel cannot reach, so a
human drives them. What a human should not have to do is hand-build a file per
failure status and hope it still triggers the one it used to.

So the corpus is generated, not committed. Every file here exists to land on
one `MusicXmlImportStatus`, named in its own filename, and each is built from
the same input the importer's own tests use for that status -- so the inputs
have one source of truth rather than two that drift.

What verifies them is worth being exact about. `test_make_score_import_fixtures.py`
checks the properties Python can see: that every fixture is written, that the
symlink dangles, that the unreadable file really is unreadable, and that the
container fixtures stay on the correct side of the size and ratio caps. It
cannot check which status a file produces -- only the C++ importer can, and
there is no committed test that does. **So when the importer changes what it
refuses, nothing here fails; the first sign is a tester finding that
`05-malformed-xml.musicxml` now imports fine.** Re-check the mapping against
`MusicXmlContainerTests.cpp` when that happens.

Deliberately not committed under src/tests/resources/: these are throwaway
inputs for a manual run, and two of them -- a dangling symlink and a mode-000
file -- would not survive a clone.

Standard library only.
"""

from __future__ import annotations

import argparse
import os
import stat
import sys
import zipfile
from pathlib import Path

# Mirrors MusicXmlImportLimits in src/platform/score/musicxml/MusicXmlImportResult.h.
# Duplicated rather than parsed out of the header: a drift here shows up as a
# fixture that no longer triggers tooLarge, which the test catches.
MAXIMUM_SOURCE_BYTES = 64 * 1024 * 1024
MAXIMUM_EXPANSION_RATIO = 200

# The real DOCTYPE, external URL and all, because a real export carries one and
# the importer must not go near the network to resolve it.
DOCTYPE = (
    '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN"'
    ' "http://www.musicxml.org/dtds/partwise.dtd">'
)

MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<container>
  <rootfiles>
    <rootfile full-path="{root}" media-type="application/vnd.recordare.musicxml+xml"/>
  </rootfiles>
</container>
"""


def attributes(divisions: int = 1, beats: int = 4, beat_type: int = 4) -> str:
    return f"""      <attributes>
        <divisions>{divisions}</divisions>
        <key><fifths>0</fifths></key>
        <time><beats>{beats}</beats><beat-type>{beat_type}</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef>
      </attributes>
"""


def note(step: str, octave: int, duration: int = 1, extra: str = "") -> str:
    return f"""      <note>
{extra}        <pitch><step>{step}</step><octave>{octave}</octave></pitch>
        <duration>{duration}</duration>
        <voice>1</voice>
      </note>
"""


def measure(number: int, body: str) -> str:
    return f'    <measure number="{number}">\n{body}    </measure>\n'


def score_document(body: str, *, title: str = "", composer: str = "") -> str:
    identification = ""
    if composer:
        identification = f"""  <identification>
    <creator type="composer">{composer}</creator>
    <encoding><software>Practice Takes fixture generator</software></encoding>
  </identification>
"""
    work = f"  <work><work-title>{title}</work-title></work>\n" if title else ""

    return f"""<?xml version="1.0" encoding="UTF-8"?>
{DOCTYPE}
<score-partwise version="4.0">
{work}{identification}  <part-list>
    <score-part id="P1">
      <part-name>Voice</part-name>
    </score-part>
  </part-list>
  <part id="P1">
{body}  </part>
</score-partwise>
"""


def write_zip(path: Path, entries: dict[str, str]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in entries.items():
            archive.writestr(name, content)


def one_note_score() -> str:
    return score_document(measure(1, attributes() + note("C", 4, 4)))


# --- The fixtures, one per status a tester has to tell apart ------------------


def clean(directory: Path) -> str:
    """imported -- the baseline, so "nothing was dropped" has something to say."""
    document = score_document(
        measure(1, attributes() + note("C", 4, 2) + note("E", 4, 2))
        + measure(2, note("G", 4, 4)),
        title="A clean import",
        composer="Nobody",
    )
    (directory / "00-clean.musicxml").write_text(document, encoding="utf-8")
    return "imported"


def lossy(directory: Path) -> str:
    """importedWithDiagnostics -- task 5.4.

    Carries one of each kind of diagnostic the window groups separately: several
    recognised-and-dropped elements, a repeated one so the occurrence count has
    something to count, and an element the importer does not recognise at all.
    """
    slurred = note("C", 4, 2, extra="        <slur type='start' number='1'/>\n")
    articulated = note(
        "E", 4, 2, extra="        <notations><articulations><staccato/></articulations></notations>\n"
    )
    ornamented = note(
        "G", 4, 2, extra="        <notations><ornaments><trill-mark/></ornaments></notations>\n"
    )
    # Repeated, so the summary's occurrence count is exercised rather than
    # merely rendered as "1".
    fingered = "".join(
        note(
            step,
            4,
            1,
            extra="        <notations><technical><fingering>2</fingering></technical></notations>\n",
        )
        for step in ("A", "B", "C", "D")
    )
    harmony = """      <harmony>
        <root><root-step>C</root-step></root>
        <kind>major</kind>
      </harmony>
"""
    unrecognised = "      <not-a-real-musicxml-element>x</not-a-real-musicxml-element>\n"

    document = score_document(
        measure(1, attributes() + harmony + slurred + articulated)
        + measure(2, ornamented + unrecognised + note("F", 4, 2))
        + measure(3, fingered),
        title="A score with content this importer drops",
        composer="Nobody",
    )
    (directory / "09-lossy.musicxml").write_text(document, encoding="utf-8")
    return "importedWithDiagnostics"


def not_found(directory: Path) -> str:
    """notFound -- a dangling symlink.

    The only way to reach this status through a file chooser. The chooser lists
    the link, the tester selects it, and `existsAsFile()` is false. A
    nonexistent path cannot be typed into an open dialog, and a directory is
    filtered out before the tester can pick one.
    """
    link = directory / "01-not-found.musicxml"
    link.unlink(missing_ok=True)
    # A bare filename, so the link dangles beside itself wherever the corpus is
    # moved to -- a path relative to the invoking shell would not survive that.
    link.symlink_to("this-target-does-not-exist.musicxml")
    return "notFound"


def unreadable(directory: Path) -> str:
    """unreadable -- a file the owner cannot read.

    No-ops for root, which can read it anyway; the checklist says so.
    """
    path = directory / "02-unreadable.musicxml"
    path.write_text(one_note_score(), encoding="utf-8")
    path.chmod(0o000)
    return "unreadable"


def too_large_ratio(directory: Path) -> str:
    """tooLarge -- a container entry past the 200:1 expansion ratio.

    Four megabytes of one repeated character, which deflates to a few kilobytes:
    a ratio no real score reaches. Chosen over a 64 MB source file so the corpus
    stays small; `--with-oversized-source` writes that one too.
    """
    filler = "A" * (4 * 1024 * 1024)
    write_zip(
        directory / "03-too-large-ratio.mxl",
        {"META-INF/container.xml": MANIFEST.format(root="score.xml"), "score.xml": filler},
    )
    return "tooLarge"


def too_large_source(directory: Path) -> str:
    """tooLarge -- a source file past the 64 MB on-disk cap, refused unparsed."""
    path = directory / "03b-too-large-source.musicxml"
    with path.open("wb") as handle:
        handle.write(b" " * (MAXIMUM_SOURCE_BYTES + 1))
    return "tooLarge"


def not_musicxml(directory: Path) -> str:
    """notMusicXml -- well-formed XML whose root is not a score."""
    (directory / "04-not-musicxml.musicxml").write_text(
        '<?xml version="1.0"?><html><body>Not a score.</body></html>\n', encoding="utf-8"
    )
    return "notMusicXml"


def not_musicxml_empty(directory: Path) -> str:
    """notMusicXml -- an empty file, which must not be mistaken for a score."""
    (directory / "04b-empty.musicxml").write_text("", encoding="utf-8")
    return "notMusicXml"


def malformed_xml(directory: Path) -> str:
    """malformedXml -- XML that is not well-formed."""
    (directory / "05-malformed-xml.musicxml").write_text(
        '<?xml version="1.0"?>\n<score-partwise>\n  <part-list>\n', encoding="utf-8"
    )
    return "malformedXml"


def invalid_container_no_manifest(directory: Path) -> str:
    """invalidContainer -- a ZIP holding a score but no META-INF/container.xml."""
    write_zip(directory / "06-invalid-container-no-manifest.mxl", {"score.xml": one_note_score()})
    return "invalidContainer"


def invalid_container_missing_entry(directory: Path) -> str:
    """invalidContainer -- a manifest naming a root the container does not hold.

    The error should name `somewhere-else.xml`; "invalid container" on its own
    tells a user nothing they can act on.
    """
    write_zip(
        directory / "06b-invalid-container-missing-entry.mxl",
        {
            "META-INF/container.xml": MANIFEST.format(root="somewhere-else.xml"),
            "score.xml": one_note_score(),
        },
    )
    return "invalidContainer"


def unsupported_document_type(directory: Path) -> str:
    """unsupportedDocumentType -- a score-timewise document."""
    (directory / "07-unsupported-document-type.musicxml").write_text(
        """<?xml version="1.0"?>
<score-timewise version="4.0">
  <part-list><score-part id="P1"><part-name>Voice</part-name></score-part></part-list>
  <measure number="1"><part id="P1"/></measure>
</score-timewise>
""",
        encoding="utf-8",
    )
    return "unsupportedDocumentType"


def structurally_invalid(directory: Path) -> str:
    """structurallyInvalid -- a part and a measure, but no events at all.

    Stated over events rather than notes: a movement of nothing but rests is
    valid and imports normally, so this measure carries neither.
    """
    (directory / "08-structurally-invalid.musicxml").write_text(
        score_document(measure(1, attributes())), encoding="utf-8"
    )
    return "structurallyInvalid"


def large(directory: Path, *, parts: int = 40, measures: int = 2000) -> str:
    """A valid score whose import takes long enough to watch -- task 5.1.

    Shipped as a container, because the numbers only work that way. Forty parts
    of two thousand measures is ~48 MB of XML, which took ~6 s to import on the
    machine this was written on -- long enough to see whether the window stays
    responsive, which a 350 ms import cannot show. As a plain file that is close
    to the 64 MB source cap; deflated it is ~0.4 MB on disk.

    Two limits are deliberately left room:

      - The expansion ratio lands around 117:1, under the 200:1 refusal. Varying
        measure numbers are what keep it there; a score of identical measures
        compresses past the limit and comes back `tooLarge` instead.
      - 48 MB of XML is a DOM of several hundred megabytes, since the parse holds
        the whole document by design. Raising `measures` raises that in step, so
        do not reach for the 256 MB decompressed cap to make the import slower.
    """
    part_list = "".join(
        f'    <score-part id="P{index}"><part-name>Part {index}</part-name></score-part>\n'
        for index in range(1, parts + 1)
    )

    bars = []
    for number in range(1, measures + 1):
        body = attributes() if number == 1 else ""
        for step in ("C", "D", "E", "F"):
            body += note(step, 4, 1)
        bars.append(measure(number, body))
    one_part = "".join(bars)

    bodies = "".join(f'  <part id="P{index}">\n{one_part}  </part>\n' for index in range(1, parts + 1))

    document = f"""<?xml version="1.0" encoding="UTF-8"?>
{DOCTYPE}
<score-partwise version="4.0">
  <work><work-title>A deliberately large score</work-title></work>
  <identification>
    <creator type="composer">Nobody</creator>
    <encoding><software>Practice Takes fixture generator</software></encoding>
  </identification>
  <part-list>
{part_list}  </part-list>
{bodies}</score-partwise>
"""
    write_zip(
        directory / "10-large.mxl",
        {"META-INF/container.xml": MANIFEST.format(root="score.xml"), "score.xml": document},
    )
    return "imported"


FIXTURES = (
    clean,
    not_found,
    unreadable,
    too_large_ratio,
    not_musicxml,
    not_musicxml_empty,
    malformed_xml,
    invalid_container_no_manifest,
    invalid_container_missing_entry,
    unsupported_document_type,
    structurally_invalid,
    lossy,
    large,
)


def clear(directory: Path) -> None:
    """Remove a previous run's files, including the unreadable one.

    chmod 000 survives a rewrite, so a second run would fail writing over it.
    """
    if not directory.exists():
        return

    for path in sorted(directory.iterdir()):
        if path.is_symlink():
            path.unlink()
        elif path.is_file():
            # The unreadable fixture is mode 000, which would refuse the unlink
            # on a restrictive filesystem; make it writable first.
            path.chmod(stat.S_IRUSR | stat.S_IWUSR)
            path.unlink()


def generate(directory: Path, *, with_oversized_source: bool = False) -> dict[str, str]:
    """Write every fixture. Returns filename -> the status it should produce."""
    clear(directory)
    directory.mkdir(parents=True, exist_ok=True)

    for fixture in FIXTURES:
        fixture(directory)
    if with_oversized_source:
        too_large_source(directory)

    return expected(directory)


def expected(directory: Path) -> dict[str, str]:
    """Filename -> the MusicXmlImportStatus it is built to produce."""
    statuses = {
        "00-clean.musicxml": "imported",
        "01-not-found.musicxml": "notFound",
        "02-unreadable.musicxml": "unreadable",
        "03-too-large-ratio.mxl": "tooLarge",
        "03b-too-large-source.musicxml": "tooLarge",
        "04-not-musicxml.musicxml": "notMusicXml",
        "04b-empty.musicxml": "notMusicXml",
        "05-malformed-xml.musicxml": "malformedXml",
        "06-invalid-container-no-manifest.mxl": "invalidContainer",
        "06b-invalid-container-missing-entry.mxl": "invalidContainer",
        "07-unsupported-document-type.musicxml": "unsupportedDocumentType",
        "08-structurally-invalid.musicxml": "structurallyInvalid",
        "09-lossy.musicxml": "importedWithDiagnostics",
        "10-large.mxl": "imported",
    }
    present = {path.name for path in directory.iterdir()} if directory.exists() else set()
    return {name: status for name, status in statuses.items() if name in present}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Write the files a tester opens to verify score import by hand."
    )
    parser.add_argument(
        "--directory",
        type=Path,
        default=Path("build/manual-score-import"),
        help="where to write the corpus (default: build/manual-score-import)",
    )
    parser.add_argument(
        "--with-oversized-source",
        action="store_true",
        help="also write the 64 MB file that trips the on-disk cap",
    )
    arguments = parser.parse_args(argv)

    statuses = generate(
        arguments.directory, with_oversized_source=arguments.with_oversized_source
    )

    print(f"Wrote {len(statuses)} fixtures to {arguments.directory}\n")
    width = max(len(name) for name in statuses)
    for name, status in sorted(statuses.items()):
        print(f"  {name:<{width}}  ->  {status}")

    if os.geteuid() == 0:
        print(
            "\nWarning: running as root. 02-unreadable.musicxml will import fine,"
            " because root ignores the permission bits.",
            file=sys.stderr,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
