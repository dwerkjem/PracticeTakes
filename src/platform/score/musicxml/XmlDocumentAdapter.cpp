#include "XmlDocumentAdapter.h"

#include <juce_core/juce_core.h>

#include <cctype>
#include <charconv>
#include <memory>
#include <utility>

// The only file that names an XML parser. See the header for why that matters.
namespace score::musicxml
{
namespace
{
// Deep enough for any real score by a wide margin, shallow enough that the
// recursive copy below cannot exhaust the stack on a hostile file.
constexpr int maximumNestingDepth = 100;

// Remove the DOCTYPE declaration, internal subset and all.
//
// This is what makes external-entity resolution and entity-expansion attacks
// impossible rather than merely bounded: there is nothing left to resolve or
// expand. JUCE's parser would otherwise tokenise an internal subset and expand
// the entities it declares, which is the billion-laughs shape, and would follow
// a SYSTEM identifier to a local file when given an input source.
//
// Scanning rather than parsing is adequate here because the DOCTYPE, if
// present, must appear in the prolog: before the root element, after only the
// XML declaration, comments, and processing instructions. Anything that looks
// like a DOCTYPE inside the document body is character data and is never
// reached.
std::string withoutDoctype(const std::string& document)
{
    std::size_t position = 0;

    const auto skipWhitespace = [&document, &position]
    {
        while (position < document.size() &&
               std::isspace(static_cast<unsigned char>(document[position])) != 0)
        {
            ++position;
        }
    };

    while (true)
    {
        skipWhitespace();

        if (position >= document.size() || document[position] != '<')
        {
            return document;
        }

        if (document.compare(position, 4, "<!--") == 0)
        {
            const std::size_t end = document.find("-->", position + 4);

            if (end == std::string::npos)
            {
                return document;
            }

            position = end + 3;

            continue;
        }

        if (document.compare(position, 2, "<?") == 0)
        {
            const std::size_t end = document.find("?>", position + 2);

            if (end == std::string::npos)
            {
                return document;
            }

            position = end + 2;

            continue;
        }

        if (document.compare(position, 9, "<!DOCTYPE") != 0)
        {
            // The root element. No DOCTYPE to remove.
            return document;
        }

        // An internal subset runs from '[' to the matching ']', and the
        // declaration ends at the first '>' after whichever of the two the
        // document actually has.
        const std::size_t declarationEnd = document.find('>', position);
        const std::size_t subsetStart = document.find('[', position);
        std::size_t end = declarationEnd;

        if (subsetStart != std::string::npos && subsetStart < declarationEnd)
        {
            const std::size_t subsetEnd = document.find(']', subsetStart);

            if (subsetEnd == std::string::npos)
            {
                return document;
            }

            end = document.find('>', subsetEnd);
        }

        if (end == std::string::npos)
        {
            return document;
        }

        return document.substr(0, position) + document.substr(end + 1);
    }
}

// Copy one JUCE element into our own tree, depth first.
//
// A JUCE document interleaves text children with element children. The text is
// collapsed into this node's value and the elements become children, which is
// the shape MusicXML actually has: an element either holds text or holds
// elements, never meaningfully both.
bool copyElement(const juce::XmlElement& source, XmlNode& destination, int depthRemaining)
{
    if (depthRemaining <= 0)
    {
        return false;
    }

    destination.name = source.getTagName().toStdString();

    destination.attributes.reserve(static_cast<std::size_t>(source.getNumAttributes()));

    for (int index = 0; index < source.getNumAttributes(); ++index)
    {
        destination.attributes.push_back(
            {source.getAttributeName(index).toStdString(),
             source.getAttributeValue(index).toStdString()});
    }

    juce::String text;

    for (const juce::XmlElement* child = source.getFirstChildElement(); child != nullptr;
         child = child->getNextElement())
    {
        if (child->isTextElement())
        {
            text += child->getText();

            continue;
        }

        XmlNode& copied = destination.children.emplace_back();

        if (!copyElement(*child, copied, depthRemaining - 1))
        {
            return false;
        }
    }

    destination.value = text.trim().toStdString();

    return true;
}
} // namespace

const XmlNode* findChild(const XmlNode& node, std::string_view name) noexcept
{
    for (const XmlNode& child : node.children)
    {
        if (child.name == name)
        {
            return &child;
        }
    }

    return nullptr;
}

std::vector<const XmlNode*> findChildren(const XmlNode& node, std::string_view name)
{
    std::vector<const XmlNode*> matches;

    for (const XmlNode& child : node.children)
    {
        if (child.name == name)
        {
            matches.push_back(&child);
        }
    }

    return matches;
}

std::string childValue(const XmlNode& node, std::string_view name)
{
    const XmlNode* child = findChild(node, name);

    return child != nullptr ? child->value : std::string{};
}

std::string attributeValue(const XmlNode& node, std::string_view name)
{
    for (const XmlAttribute& attribute : node.attributes)
    {
        if (attribute.name == name)
        {
            return attribute.value;
        }
    }

    return {};
}

std::optional<int> intAttribute(const XmlNode& node, std::string_view name)
{
    const std::string text = attributeValue(node, name);

    if (text.empty())
    {
        return std::nullopt;
    }

    int value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);

    // Trailing junk is a rejection, not a partial success: "3x" is not 3.
    if (parsed.ec != std::errc{} || parsed.ptr != end)
    {
        return std::nullopt;
    }

    return value;
}

bool hasAttribute(const XmlNode& node, std::string_view name) noexcept
{
    for (const XmlAttribute& attribute : node.attributes)
    {
        if (attribute.name == name)
        {
            return true;
        }
    }

    return false;
}

XmlParseResult parseXmlDocument(const std::string& document)
{
    XmlParseResult result;

    if (document.empty())
    {
        result.status = XmlParseStatus::empty;
        result.error = "The document is empty.";

        return result;
    }

    juce::XmlDocument parser(
        juce::String(juce::CharPointer_UTF8(withoutDoctype(document).c_str())));
    const std::unique_ptr<juce::XmlElement> root = parser.getDocumentElement();

    if (root == nullptr)
    {
        const juce::String error = parser.getLastParseError();

        result.status = XmlParseStatus::malformed;
        result.error = error.isNotEmpty() ? error.toStdString()
                                          : std::string{"The document is not valid XML."};

        return result;
    }

    XmlNode copied;

    if (!copyElement(*root, copied, maximumNestingDepth))
    {
        result.status = XmlParseStatus::malformed;
        result.error = "The document nests elements more deeply than this importer accepts.";

        return result;
    }

    if (copied.name.empty())
    {
        result.status = XmlParseStatus::empty;
        result.error = "The document contains no XML elements.";

        return result;
    }

    result.status = XmlParseStatus::parsed;
    result.root = std::move(copied);

    return result;
}
} // namespace score::musicxml
