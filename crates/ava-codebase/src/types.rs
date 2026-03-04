#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SearchDocument {
    pub path: String,
    pub content: String,
    pub language: Option<String>,
}

impl SearchDocument {
    pub fn new(path: impl Into<String>, content: impl Into<String>) -> Self {
        Self {
            path: path.into(),
            content: content.into(),
            language: None,
        }
    }

    pub fn with_language(mut self, language: impl Into<String>) -> Self {
        self.language = Some(language.into());
        self
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct SearchHit {
    pub path: String,
    pub score: f32,
    pub snippet: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SearchQuery {
    pub query: String,
    pub max_results: usize,
}

impl SearchQuery {
    pub fn new(query: impl Into<String>) -> Self {
        Self {
            query: query.into(),
            max_results: 10,
        }
    }

    pub fn with_max_results(mut self, max_results: usize) -> Self {
        self.max_results = max_results;
        self
    }
}
