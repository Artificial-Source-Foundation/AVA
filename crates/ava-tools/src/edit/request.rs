#[derive(Debug, Clone, Default)]
pub struct EditRequest {
    pub content: String,
    pub old_text: String,
    pub new_text: String,
    pub before_anchor: Option<String>,
    pub after_anchor: Option<String>,
    pub line_number: Option<usize>,
    pub regex_pattern: Option<String>,
    pub occurrence: Option<usize>,
}

impl EditRequest {
    pub fn new(
        content: impl Into<String>,
        old_text: impl Into<String>,
        new_text: impl Into<String>,
    ) -> Self {
        Self {
            content: content.into(),
            old_text: old_text.into(),
            new_text: new_text.into(),
            ..Self::default()
        }
    }

    pub fn with_anchors(mut self, before: impl Into<String>, after: impl Into<String>) -> Self {
        self.before_anchor = Some(before.into());
        self.after_anchor = Some(after.into());
        self
    }

    pub fn with_line_number(mut self, line_number: usize) -> Self {
        self.line_number = Some(line_number);
        self
    }

    pub fn with_regex_pattern(mut self, regex_pattern: impl Into<String>) -> Self {
        self.regex_pattern = Some(regex_pattern.into());
        self
    }

    pub fn with_occurrence(mut self, occurrence: usize) -> Self {
        self.occurrence = Some(occurrence);
        self
    }
}
