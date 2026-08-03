#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The seam between this importer and whatever parses XML for it.
//
// A parse produces the plain owned tree below, so no parser type crosses this
// boundary and replacing the parser is one file. The copy that costs is
// deliberate: handing out views into a parser's own document would leak its
// lifetime rules and its types into every file that walks a score. Source
// documents are bounded by `maximumSourceBytes`, so the copy is bounded too.
//
// This header has no JUCE dependency even though the implementation currently
// uses juce::parseXML -- that is the point of the seam. `.mxl` container
// handling needs juce::ZipFile and lives in MusicXmlContainer instead.
namespace score::musicxml
{
struct XmlAttribute
{
    std::string name;
    std::string value;
};

// One element of a parsed document: its name, its text value, its attributes,
// and its children **in source order**.
//
// Source order is load-bearing, not incidental. MusicXML writes a measure as a
// stream of <note>, <backup>, and <forward> elements whose meaning is entirely
// positional -- a <backup> moves the write cursor back over whatever preceded
// it. Sorting or grouping children by name would destroy the document.
struct XmlNode
{
    std::string name;

    // Text content, trimmed. Empty for a container element such as <measure>.
    std::string value;

    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
};

// First child with this name, or nullptr. Only direct children are searched.
[[nodiscard]] const XmlNode* findChild(const XmlNode& node, std::string_view name) noexcept;

// Every direct child with this name, in source order.
[[nodiscard]] std::vector<const XmlNode*> findChildren(const XmlNode& node, std::string_view name);

// Text value of the first child with this name, or an empty string when there
// is no such child. The two cases are indistinguishable on purpose: for every
// MusicXML element this importer reads, an absent element and one holding no
// text mean the same thing.
[[nodiscard]] std::string childValue(const XmlNode& node, std::string_view name);

// Attribute value, or an empty string when the attribute is absent.
[[nodiscard]] std::string attributeValue(const XmlNode& node, std::string_view name);

// Attribute parsed as an integer, or nullopt when it is absent, empty, not a
// number, or has trailing junk. Callers distinguish "not stated" from "stated
// wrongly" by checking presence separately when it matters.
[[nodiscard]] std::optional<int> intAttribute(const XmlNode& node, std::string_view name);

// Whether the attribute is present at all, whatever its value.
[[nodiscard]] bool hasAttribute(const XmlNode& node, std::string_view name) noexcept;

enum class XmlParseStatus
{
    parsed,

    // The document held no elements.
    empty,

    // Not well-formed XML.
    malformed
};

struct XmlParseResult
{
    XmlParseStatus status = XmlParseStatus::malformed;
    std::optional<XmlNode> root;

    // Empty when the status is `parsed`. This is the parser's own message; it
    // names what went wrong but not where, which is why diagnostics lead with a
    // musical location instead (design.md, decision 5).
    std::string error;
};

// Parse a whole XML document held in memory.
//
// Two things this does that the underlying parser does not, both of which are
// the reason it is worth having a function here rather than calling the parser
// directly from the importer:
//
//  - **The DOCTYPE is removed before parsing.** A MusicXML document declares an
//    external DTD by URL, and its internal subset may declare entities. Neither
//    is wanted: resolving an external reference on a file-open path is an
//    I/O stall and an XXE-shaped hole, and inline entity expansion is the
//    classic XML denial of service. Removing the declaration outright makes
//    both impossible rather than merely bounded. Nothing is lost -- the five
//    predefined entities and numeric character references do not come from the
//    DTD, and MusicXML content uses nothing else.
//  - **Nesting depth is bounded**, so a document nested thousands deep is a
//    clean failure rather than an exhausted stack.
//
// No network access occurs at any point.
[[nodiscard]] XmlParseResult parseXmlDocument(const std::string& document);
} // namespace score::musicxml
