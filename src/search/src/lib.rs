//! The application's search index: Tantivy behind a C interface
//! (openspec/changes/archive/2026-11-10-everything-search).
//!
//! Everything searchable -- a setting, a tool, a command, a song, a take, a
//! line of the open score's lyrics -- is a document with a `kind`, an `id` the
//! application acts on, a `title` shown in the results, and `keywords` and a
//! `body` that are searched but not shown. The application replaces a kind's
//! documents whenever what it describes changes, and asks for the best matches
//! of what the reader has typed so far.
//!
//! The index lives in memory: it holds a few thousand short documents and is
//! rebuilt from the application's own state at every launch, so there is
//! nothing on disk to version or to go stale.
//!
//! The interface is plain C (see `src/platform/search/SearchIndexFfi.h`): no
//! JSON, no callbacks, only strings in and an opaque result set out. Nothing
//! here may unwind into C++, so every entry point catches a panic and reports
//! failure instead.

use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};

use tantivy::collector::TopDocs;
use tantivy::query::{BooleanQuery, BoostQuery, FuzzyTermQuery, Occur, Query, TermQuery};
use tantivy::schema::{
    Field, IndexRecordOption, Schema, TextFieldIndexing, TextOptions, Value, STORED, STRING,
};
use tantivy::tokenizer::{
    AsciiFoldingFilter, LowerCaser, RemoveLongFilter, SimpleTokenizer, TextAnalyzer,
};
use tantivy::{doc, Index, IndexReader, IndexWriter, ReloadPolicy, TantivyDocument, Term};

/// The analyser every searched field uses: words split on anything that is not
/// a letter or digit, lower-cased, and folded to ASCII, so "Gesellen" is found
/// by "gesellen" and "Gustav Mahler" by "mahler"; "Né" by "ne".
const ANALYSER: &str = "practice_takes";

/// The smallest writer budget Tantivy accepts. The index is small; this is
/// spent at most once per commit.
const WRITER_BUDGET: usize = 15_000_000;

/// Words shorter than this match by prefix only. Longer ones may also be one
/// letter out, so "metronme" finds the metronome, but "key" does not find
/// "kef".
const FUZZY_FROM_LETTERS: usize = 4;

/// The most results a query returns, whatever it asks.
const MOST_RESULTS: u32 = 200;

struct Fields {
    kind: Field,
    id: Field,
    title: Field,
    detail: Field,
    keywords: Field,
    body: Field,
}

/// An index and the writer and reader that go with it.
pub struct SearchIndex {
    index: Index,
    writer: IndexWriter<TantivyDocument>,
    reader: IndexReader,
    fields: Fields,
}

/// One match: what to show, and what to act on.
pub struct Hit {
    kind: CString,
    id: CString,
    title: CString,
    detail: CString,
    score: f32,
}

/// The matches of one query, best first.
pub struct SearchResults {
    hits: Vec<Hit>,
}

fn searched_text() -> TextOptions {
    TextOptions::default().set_indexing_options(
        TextFieldIndexing::default()
            .set_tokenizer(ANALYSER)
            .set_index_option(IndexRecordOption::WithFreqsAndPositions),
    )
}

impl SearchIndex {
    pub fn new() -> tantivy::Result<SearchIndex> {
        let mut builder = Schema::builder();
        let fields = Fields {
            kind: builder.add_text_field("kind", STRING | STORED),
            id: builder.add_text_field("id", STRING | STORED),
            title: builder.add_text_field("title", searched_text() | STORED),
            detail: builder.add_text_field("detail", searched_text() | STORED),
            keywords: builder.add_text_field("keywords", searched_text()),
            body: builder.add_text_field("body", searched_text()),
        };
        let index = Index::create_in_ram(builder.build());
        index.tokenizers().register(
            ANALYSER,
            TextAnalyzer::builder(SimpleTokenizer::default())
                .filter(RemoveLongFilter::limit(40))
                .filter(LowerCaser)
                .filter(AsciiFoldingFilter)
                .build(),
        );
        let writer = index.writer_with_num_threads(1, WRITER_BUDGET)?;
        let reader = index
            .reader_builder()
            .reload_policy(ReloadPolicy::Manual)
            .try_into()?;
        Ok(SearchIndex {
            index,
            writer,
            reader,
            fields,
        })
    }

    /// Forget every document of `kind`. Takes effect at the next commit.
    pub fn clear_kind(&mut self, kind: &str) {
        self.writer
            .delete_term(Term::from_field_text(self.fields.kind, kind));
    }

    /// Add a document. Takes effect at the next commit.
    pub fn add(
        &mut self,
        kind: &str,
        id: &str,
        title: &str,
        detail: &str,
        keywords: &str,
        body: &str,
    ) -> tantivy::Result<()> {
        let fields = &self.fields;
        self.writer.add_document(doc!(
            fields.kind => kind,
            fields.id => id,
            fields.title => title,
            fields.detail => detail,
            fields.keywords => keywords,
            fields.body => body,
        ))?;
        Ok(())
    }

    /// Make every clear and add since the last commit searchable.
    pub fn commit(&mut self) -> tantivy::Result<()> {
        self.writer.commit()?;
        self.reader.reload()
    }

    /// The words of `text`, as the index holds them.
    fn words(&self, text: &str) -> Vec<String> {
        let mut words = Vec::new();
        if let Some(mut analyser) = self.index.tokenizers().get(ANALYSER) {
            let mut stream = analyser.token_stream(text);
            while stream.advance() {
                words.push(stream.token().text.clone());
            }
        }
        words
    }

    /// One word's query: the word itself, any word it begins, and, when long
    /// enough, one a letter out -- in the title above the detail and keywords,
    /// and those above the body.
    fn word_query(&self, word: &str) -> Box<dyn Query> {
        let fields = &self.fields;
        let weighted = [
            (fields.title, 4.0_f32),
            (fields.keywords, 2.5),
            (fields.detail, 1.5),
            (fields.body, 1.0),
        ];
        let mut either: Vec<(Occur, Box<dyn Query>)> = Vec::new();
        for (field, weight) in weighted {
            let term = Term::from_field_text(field, word);
            either.push((
                Occur::Should,
                Box::new(BoostQuery::new(
                    Box::new(TermQuery::new(term.clone(), IndexRecordOption::WithFreqs)),
                    weight * 2.0,
                )),
            ));
            either.push((
                Occur::Should,
                Box::new(BoostQuery::new(
                    Box::new(FuzzyTermQuery::new_prefix(term.clone(), 0, true)),
                    weight,
                )),
            ));
            if word.chars().count() >= FUZZY_FROM_LETTERS {
                either.push((
                    Occur::Should,
                    Box::new(BoostQuery::new(
                        Box::new(FuzzyTermQuery::new_prefix(term, 1, true)),
                        weight * 0.4,
                    )),
                ));
            }
        }
        Box::new(BooleanQuery::new(either))
    }

    /// The best `limit` matches of `text`. Every word typed must match
    /// somewhere, so each word narrows the results; the last may be
    /// half-typed. Nothing typed matches nothing.
    pub fn search(&self, text: &str, limit: u32) -> tantivy::Result<Vec<Hit>> {
        let words = self.words(text);
        if words.is_empty() || limit == 0 {
            return Ok(Vec::new());
        }
        let every: Vec<(Occur, Box<dyn Query>)> = words
            .iter()
            .map(|word| (Occur::Must, self.word_query(word)))
            .collect();
        let query = BooleanQuery::new(every);
        let searcher = self.reader.searcher();
        let limit = limit.min(MOST_RESULTS) as usize;
        let found = searcher.search(&query, &TopDocs::with_limit(limit).order_by_score())?;

        let text_of = |document: &TantivyDocument, field: Field| -> CString {
            let value = document
                .get_first(field)
                .and_then(|value| value.as_str())
                .unwrap_or("");
            CString::new(value.replace('\0', "")).unwrap_or_default()
        };
        let mut hits = Vec::with_capacity(found.len());
        for (score, address) in found {
            let document: TantivyDocument = searcher.doc(address)?;
            hits.push(Hit {
                kind: text_of(&document, self.fields.kind),
                id: text_of(&document, self.fields.id),
                title: text_of(&document, self.fields.title),
                detail: text_of(&document, self.fields.detail),
                score,
            });
        }
        Ok(hits)
    }
}

// ---------------------------------------------------------------------------
// The C interface. Every pointer argument may be null, and a null or invalid
// UTF-8 string is treated as empty; nothing here trusts its caller to have
// checked.
// ---------------------------------------------------------------------------

fn text<'a>(pointer: *const c_char) -> &'a str {
    if pointer.is_null() {
        return "";
    }
    // SAFETY: the caller passes a NUL-terminated string that outlives the
    // call, as the header requires.
    unsafe { CStr::from_ptr(pointer) }.to_str().unwrap_or("")
}

fn guarded<T>(fallback: T, body: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or(fallback)
}

/// A new, empty index, or null if one cannot be made.
#[no_mangle]
pub extern "C" fn pt_search_create() -> *mut SearchIndex {
    guarded(std::ptr::null_mut(), || match SearchIndex::new() {
        Ok(index) => Box::into_raw(Box::new(index)),
        Err(_) => std::ptr::null_mut(),
    })
}

/// # Safety
/// `index` is null or came from `pt_search_create` and is not used again.
#[no_mangle]
pub unsafe extern "C" fn pt_search_destroy(index: *mut SearchIndex) {
    if !index.is_null() {
        guarded((), || drop(unsafe { Box::from_raw(index) }));
    }
}

/// # Safety
/// `index` is null or a live index from `pt_search_create`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_clear_kind(
    index: *mut SearchIndex,
    kind: *const c_char,
) -> bool {
    let Some(index) = (unsafe { index.as_mut() }) else {
        return false;
    };
    guarded(false, || {
        index.clear_kind(text(kind));
        true
    })
}

/// # Safety
/// `index` is null or a live index from `pt_search_create`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_add(
    index: *mut SearchIndex,
    kind: *const c_char,
    id: *const c_char,
    title: *const c_char,
    detail: *const c_char,
    keywords: *const c_char,
    body: *const c_char,
) -> bool {
    let Some(index) = (unsafe { index.as_mut() }) else {
        return false;
    };
    guarded(false, || {
        index
            .add(
                text(kind),
                text(id),
                text(title),
                text(detail),
                text(keywords),
                text(body),
            )
            .is_ok()
    })
}

/// # Safety
/// `index` is null or a live index from `pt_search_create`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_commit(index: *mut SearchIndex) -> bool {
    let Some(index) = (unsafe { index.as_mut() }) else {
        return false;
    };
    guarded(false, || index.commit().is_ok())
}

/// The best `limit` matches of `query`, never null for a live index; destroy
/// them with `pt_search_results_destroy`.
///
/// # Safety
/// `index` is null or a live index from `pt_search_create`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_query(
    index: *const SearchIndex,
    query: *const c_char,
    limit: u32,
) -> *mut SearchResults {
    let Some(index) = (unsafe { index.as_ref() }) else {
        return std::ptr::null_mut();
    };
    guarded(std::ptr::null_mut(), || {
        let hits = index.search(text(query), limit).unwrap_or_default();
        Box::into_raw(Box::new(SearchResults { hits }))
    })
}

/// # Safety
/// `results` is null or came from `pt_search_query`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_count(results: *const SearchResults) -> u32 {
    unsafe { results.as_ref() }.map_or(0, |results| results.hits.len() as u32)
}

fn hit<'a>(results: *const SearchResults, position: u32) -> Option<&'a Hit> {
    // SAFETY: as each caller's contract states.
    unsafe { results.as_ref() }.and_then(|results| results.hits.get(position as usize))
}

const EMPTY: &CStr = c"";

/// The strings below live as long as `results`.
///
/// # Safety
/// `results` is null or came from `pt_search_query` and is not yet destroyed.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_kind(
    results: *const SearchResults,
    position: u32,
) -> *const c_char {
    hit(results, position).map_or(EMPTY.as_ptr(), |hit| hit.kind.as_ptr())
}

/// # Safety
/// As `pt_search_results_kind`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_id(
    results: *const SearchResults,
    position: u32,
) -> *const c_char {
    hit(results, position).map_or(EMPTY.as_ptr(), |hit| hit.id.as_ptr())
}

/// # Safety
/// As `pt_search_results_kind`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_title(
    results: *const SearchResults,
    position: u32,
) -> *const c_char {
    hit(results, position).map_or(EMPTY.as_ptr(), |hit| hit.title.as_ptr())
}

/// # Safety
/// As `pt_search_results_kind`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_detail(
    results: *const SearchResults,
    position: u32,
) -> *const c_char {
    hit(results, position).map_or(EMPTY.as_ptr(), |hit| hit.detail.as_ptr())
}

/// # Safety
/// As `pt_search_results_kind`.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_score(
    results: *const SearchResults,
    position: u32,
) -> f32 {
    hit(results, position).map_or(0.0, |hit| hit.score)
}

/// # Safety
/// `results` is null or came from `pt_search_query` and is not used again.
#[no_mangle]
pub unsafe extern "C" fn pt_search_results_destroy(results: *mut SearchResults) {
    if !results.is_null() {
        guarded((), || drop(unsafe { Box::from_raw(results) }));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn index() -> SearchIndex {
        let mut index = SearchIndex::new().expect("an index");
        index
            .add(
                "setting",
                "metronome",
                "Metronome",
                "Practice",
                "click beat",
                "",
            )
            .unwrap();
        index
            .add(
                "song",
                "mahler",
                "Die zwei blauen Augen",
                "Gustav Mahler",
                "",
                "Voice, Piano",
            )
            .unwrap();
        index.commit().unwrap();
        index
    }

    fn ids(hits: &[Hit]) -> Vec<String> {
        hits.iter()
            .map(|hit| hit.id.to_str().unwrap().to_owned())
            .collect()
    }

    #[test]
    fn half_a_word_finds_it() {
        assert_eq!(ids(&index().search("metr", 10).unwrap()), ["metronome"]);
    }

    #[test]
    fn a_long_word_one_letter_out_still_finds_it() {
        assert_eq!(ids(&index().search("metronme", 10).unwrap()), ["metronome"]);
    }

    #[test]
    fn every_word_must_match() {
        let index = index();
        assert_eq!(ids(&index.search("mahler augen", 10).unwrap()), ["mahler"]);
        assert!(index.search("mahler metronome", 10).unwrap().is_empty());
    }

    #[test]
    fn nothing_typed_finds_nothing() {
        assert!(index().search("  ", 10).unwrap().is_empty());
    }

    #[test]
    fn a_kind_replaced_is_replaced() {
        let mut index = index();
        index.clear_kind("song");
        index.add("song", "aria", "Aria", "Bach", "", "").unwrap();
        index.commit().unwrap();
        assert!(index.search("mahler", 10).unwrap().is_empty());
        assert_eq!(ids(&index.search("aria", 10).unwrap()), ["aria"]);
        assert_eq!(ids(&index.search("metronome", 10).unwrap()), ["metronome"]);
    }
}
