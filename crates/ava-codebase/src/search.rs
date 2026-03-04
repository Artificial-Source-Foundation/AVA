use std::sync::Mutex;

use tantivy::collector::TopDocs;
use tantivy::query::QueryParser;
use tantivy::schema::{Field, Value, STORED, STRING, TEXT};
use tantivy::{doc, Index, IndexReader, IndexWriter, ReloadPolicy, Term};

use crate::error::{CodebaseError, Result};
use crate::types::{SearchDocument, SearchHit, SearchQuery};

pub struct SearchIndex {
    index: Index,
    reader: IndexReader,
    writer: Mutex<IndexWriter>,
    path_field: Field,
    content_field: Field,
}

impl SearchIndex {
    pub fn new() -> Result<Self> {
        let mut schema_builder = tantivy::schema::Schema::builder();
        let path_field = schema_builder.add_text_field("path", STRING | STORED);
        let content_field = schema_builder.add_text_field("content", TEXT | STORED);
        let schema = schema_builder.build();

        let index = Index::create_in_ram(schema);
        let writer = index.writer(50_000_000)?;
        let reader = index
            .reader_builder()
            .reload_policy(ReloadPolicy::Manual)
            .try_into()?;

        Ok(Self {
            index,
            reader,
            writer: Mutex::new(writer),
            path_field,
            content_field,
        })
    }

    pub fn add_document(&self, document: &SearchDocument) -> Result<()> {
        let writer = self
            .writer
            .lock()
            .map_err(|_| CodebaseError::Tantivy("index writer lock poisoned".to_string()))?;
        writer.add_document(doc!(
            self.path_field => document.path.clone(),
            self.content_field => document.content.clone()
        ))?;
        Ok(())
    }

    pub fn update_document(&self, path: &str, document: &SearchDocument) -> Result<()> {
        let writer = self
            .writer
            .lock()
            .map_err(|_| CodebaseError::Tantivy("index writer lock poisoned".to_string()))?;
        writer.delete_term(Term::from_field_text(self.path_field, path));
        writer.add_document(doc!(
            self.path_field => document.path.clone(),
            self.content_field => document.content.clone()
        ))?;
        Ok(())
    }

    pub fn commit(&self) -> Result<()> {
        let mut writer = self
            .writer
            .lock()
            .map_err(|_| CodebaseError::Tantivy("index writer lock poisoned".to_string()))?;
        writer.commit()?;
        self.reader.reload()?;
        Ok(())
    }

    pub fn search(&self, query: &SearchQuery) -> Result<Vec<SearchHit>> {
        if query.query.trim().is_empty() {
            return Err(CodebaseError::InvalidQuery(
                "query cannot be empty".to_string(),
            ));
        }

        let searcher = self.reader.searcher();
        let parser = QueryParser::for_index(&self.index, vec![self.content_field, self.path_field]);
        let parsed = parser
            .parse_query(&query.query)
            .map_err(|e| CodebaseError::InvalidQuery(e.to_string()))?;

        let docs = searcher.search(&parsed, &TopDocs::with_limit(query.max_results))?;
        let mut hits = Vec::with_capacity(docs.len());

        for (score, address) in docs {
            let doc = searcher.doc::<tantivy::schema::document::TantivyDocument>(address)?;
            let path = doc
                .get_first(self.path_field)
                .and_then(|v| v.as_str())
                .unwrap_or_default()
                .to_string();
            let content = doc
                .get_first(self.content_field)
                .and_then(|v| v.as_str())
                .unwrap_or_default()
                .to_string();

            hits.push(SearchHit {
                path,
                score,
                snippet: make_snippet(&content),
            });
        }

        Ok(hits)
    }
}

fn make_snippet(content: &str) -> String {
    const MAX_SNIPPET: usize = 120;
    if content.chars().count() <= MAX_SNIPPET {
        return content.to_string();
    }
    let end = content
        .char_indices()
        .nth(MAX_SNIPPET)
        .map_or(content.len(), |(idx, _)| idx);
    format!("{}...", &content[..end])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_and_search_document() {
        let index = SearchIndex::new().unwrap();
        index
            .add_document(&SearchDocument::new(
                "src/main.rs",
                "fn main() { println!(\"hi\") }",
            ))
            .unwrap();
        index.commit().unwrap();

        let results = index.search(&SearchQuery::new("println")).unwrap();
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].path, "src/main.rs");
    }

    #[test]
    fn update_document_replaces_content() {
        let index = SearchIndex::new().unwrap();
        index
            .add_document(&SearchDocument::new("src/lib.rs", "alpha beta gamma"))
            .unwrap();
        index.commit().unwrap();

        index
            .update_document(
                "src/lib.rs",
                &SearchDocument::new("src/lib.rs", "delta epsilon"),
            )
            .unwrap();
        index.commit().unwrap();

        assert!(index.search(&SearchQuery::new("gamma")).unwrap().is_empty());
        assert_eq!(index.search(&SearchQuery::new("delta")).unwrap().len(), 1);
    }

    #[test]
    fn max_results_is_respected() {
        let index = SearchIndex::new().unwrap();
        for i in 0..5 {
            index
                .add_document(&SearchDocument::new(format!("file{i}.rs"), "common token"))
                .unwrap();
        }
        index.commit().unwrap();

        let results = index
            .search(&SearchQuery::new("common").with_max_results(2))
            .unwrap();
        assert_eq!(results.len(), 2);
    }

    #[test]
    fn empty_query_fails() {
        let index = SearchIndex::new().unwrap();
        let err = index.search(&SearchQuery::new("   ")).unwrap_err();
        assert!(matches!(err, CodebaseError::InvalidQuery(_)));
    }
}
