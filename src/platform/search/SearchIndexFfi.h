#pragma once

#include <cstdint>

// The C interface of the Rust crate in src/search, which holds the search
// index (openspec/changes/archive/2026-11-10-everything-search). Declared here, by hand, to match
// src/search/src/lib.rs; SearchIndex is the only caller.
//
// Strings in are NUL-terminated UTF-8 that need only last the call. Strings out
// live as long as the result set they came from. Any pointer may be null.
// Nothing here throws or unwinds.
extern "C"
{
    struct PtSearchIndex;
    struct PtSearchResults;

    PtSearchIndex* pt_search_create();
    void pt_search_destroy(PtSearchIndex* index);

    bool pt_search_clear_kind(PtSearchIndex* index, const char* kind);
    bool pt_search_add(
        PtSearchIndex* index,
        const char* kind,
        const char* id,
        const char* title,
        const char* detail,
        const char* keywords,
        const char* body);
    bool pt_search_commit(PtSearchIndex* index);

    PtSearchResults*
    pt_search_query(const PtSearchIndex* index, const char* query, std::uint32_t limit);
    std::uint32_t pt_search_results_count(const PtSearchResults* results);
    const char* pt_search_results_kind(const PtSearchResults* results, std::uint32_t position);
    const char* pt_search_results_id(const PtSearchResults* results, std::uint32_t position);
    const char* pt_search_results_title(const PtSearchResults* results, std::uint32_t position);
    const char* pt_search_results_detail(const PtSearchResults* results, std::uint32_t position);
    float pt_search_results_score(const PtSearchResults* results, std::uint32_t position);
    void pt_search_results_destroy(PtSearchResults* results);
}
