#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The application's search index (no JUCE dependency): Tantivy, in the Rust
// crate under src/search, behind this class (openspec/changes/everything-search).
//
// Everything searchable is a `SearchDocument` of some kind -- a setting, a
// tool, a command, a song, a take, a line of lyrics. The shell replaces a
// kind's documents whenever what they describe changes, and asks for the best
// matches of what the reader has typed so far.
//
// One thread at a time: the shell builds and queries it on the message thread.
// A query of a few thousand documents takes well under a millisecond.
namespace search
{
struct SearchDocument
{
    // What the application acts on when the result is chosen: a setting's
    // catalogue key, a tool's id, a song's file.
    std::string id;

    // Shown, and searched first.
    std::string title;

    // Shown under the title -- the panel a setting is in, a song's composer --
    // and searched.
    std::string detail;

    // Searched, not shown: other words a reader might type for it.
    std::string keywords;

    // Searched, not shown, weighed least: a setting's description, a song's
    // parts.
    std::string body;
};

struct SearchHit
{
    std::string kind;
    std::string id;
    std::string title;
    std::string detail;
    float score = 0.0f;
};

class SearchIndex
{
  public:
    SearchIndex();
    ~SearchIndex();

    SearchIndex(const SearchIndex&) = delete;
    SearchIndex& operator=(const SearchIndex&) = delete;
    SearchIndex(SearchIndex&&) noexcept;
    SearchIndex& operator=(SearchIndex&&) noexcept;

    // False when the index could not be made; then every search finds
    // nothing, and the application carries on without it.
    [[nodiscard]] bool usable() const noexcept;

    // Every document of `kind` becomes `documents`, and is searchable on
    // return. False, changing nothing searchable, when the index refused.
    bool replace(std::string_view kind, const std::vector<SearchDocument>& documents);

    // The best `limit` matches of `query`, best first. Every word must match;
    // the last may be half-typed, and a long word may be a letter out.
    [[nodiscard]] std::vector<SearchHit> search(std::string_view query, std::size_t limit) const;

  private:
    struct Handle;
    std::unique_ptr<Handle> handle;
};
} // namespace search
