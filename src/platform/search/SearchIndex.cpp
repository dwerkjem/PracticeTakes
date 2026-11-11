#include "SearchIndex.h"

#include <algorithm>
#include <cstdint>

#include "SearchIndexFfi.h"

namespace search
{
struct SearchIndex::Handle
{
    PtSearchIndex* index = nullptr;

    Handle() : index(pt_search_create()) {}

    ~Handle()
    {
        pt_search_destroy(index);
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&&) = delete;
    Handle& operator=(Handle&&) = delete;
};

namespace
{
// The crate takes NUL-terminated strings, so a string with a NUL in it is cut
// there rather than misread.
std::string terminated(std::string_view text)
{
    return std::string(text.substr(0, text.find('\0')));
}

std::string copied(const char* text)
{
    return text != nullptr ? std::string(text) : std::string();
}

// Releases a result set however the reading ends.
struct Results
{
    PtSearchResults* results = nullptr;

    explicit Results(PtSearchResults* given) : results(given) {}

    ~Results()
    {
        pt_search_results_destroy(results);
    }

    Results(const Results&) = delete;
    Results& operator=(const Results&) = delete;
    Results(Results&&) = delete;
    Results& operator=(Results&&) = delete;
};
} // namespace

SearchIndex::SearchIndex() : handle(std::make_unique<Handle>()) {}

SearchIndex::~SearchIndex() = default;
SearchIndex::SearchIndex(SearchIndex&&) noexcept = default;
SearchIndex& SearchIndex::operator=(SearchIndex&&) noexcept = default;

bool SearchIndex::usable() const noexcept
{
    return handle != nullptr && handle->index != nullptr;
}

bool SearchIndex::replace(std::string_view kind, const std::vector<SearchDocument>& documents)
{
    if (!usable())
    {
        return false;
    }
    const std::string kindText = terminated(kind);
    bool accepted = pt_search_clear_kind(handle->index, kindText.c_str());
    for (const SearchDocument& document : documents)
    {
        accepted = pt_search_add(
                       handle->index, kindText.c_str(), terminated(document.id).c_str(),
                       terminated(document.title).c_str(), terminated(document.detail).c_str(),
                       terminated(document.keywords).c_str(), terminated(document.body).c_str()) &&
                   accepted;
    }
    return pt_search_commit(handle->index) && accepted;
}

std::vector<SearchHit> SearchIndex::search(std::string_view query, std::size_t limit) const
{
    std::vector<SearchHit> hits;
    if (!usable() || limit == 0)
    {
        return hits;
    }
    const std::string text = terminated(query);
    const Results results(pt_search_query(
        handle->index, text.c_str(),
        static_cast<std::uint32_t>(std::min<std::size_t>(limit, UINT32_MAX))));
    const std::uint32_t count = pt_search_results_count(results.results);
    hits.reserve(count);
    for (std::uint32_t position = 0; position < count; ++position)
    {
        hits.push_back(SearchHit{
            copied(pt_search_results_kind(results.results, position)),
            copied(pt_search_results_id(results.results, position)),
            copied(pt_search_results_title(results.results, position)),
            copied(pt_search_results_detail(results.results, position)),
            pt_search_results_score(results.results, position)});
    }
    return hits;
}
} // namespace search
