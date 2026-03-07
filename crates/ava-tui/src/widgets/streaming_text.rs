#[derive(Debug, Default)]
pub struct StreamingText {
    pub full_text: String,
    pub pending: String,
}

impl StreamingText {
    pub fn push_chunk(&mut self, chunk: &str) {
        self.pending.push_str(chunk);
    }

    pub fn flush(&mut self) {
        self.full_text.push_str(&self.pending);
        self.pending.clear();
    }

    pub fn content(&self) -> String {
        format!("{}{}", self.full_text, self.pending)
    }
}
