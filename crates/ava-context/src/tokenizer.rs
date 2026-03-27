//! Accurate BPE token counting using tiktoken-rs.
//!
//! Provides model-aware token counting with automatic encoding selection:
//! - `o200k_base` for GPT-4o / o-series models
//! - `cl100k_base` for Claude, GPT-4, GPT-3.5, and most other models
//!
//! Encodings are cached globally via `OnceLock`, so repeated calls are cheap
//! after the initial (one-time) BPE table construction.

use std::sync::OnceLock;

use tiktoken_rs::CoreBPE;

/// Cached cl100k_base encoder (used for most models including Claude).
static CL100K: OnceLock<CoreBPE> = OnceLock::new();

/// Cached o200k_base encoder (used for GPT-4o / o-series).
static O200K: OnceLock<CoreBPE> = OnceLock::new();

/// The BPE encoding to use for token counting.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TokenEncoding {
    /// cl100k_base — Claude, GPT-4, GPT-3.5, and most models.
    Cl100kBase,
    /// o200k_base — GPT-4o, o1, o3, o4-mini.
    O200kBase,
}

impl TokenEncoding {
    /// Select the appropriate encoding for a model name.
    ///
    /// Rules:
    /// - GPT-4o, GPT-4.1, o1, o3, o4-mini -> o200k_base
    /// - Everything else (Claude, GPT-4, Gemini, Ollama, etc.) -> cl100k_base
    ///
    /// cl100k_base is a reasonable approximation for non-OpenAI models too —
    /// it shares the same BPE principles and produces counts within ~5% of
    /// provider-native tokenizers for English text and code.
    pub fn for_model(model: &str) -> Self {
        let m = model.to_lowercase();
        if m.contains("gpt-4o")
            || m.contains("gpt-4.1") // GPT-4.1 uses o200k_base
            || m.starts_with("o1")
            || m.starts_with("o3")
            || m.starts_with("o4")
            || m.contains("chatgpt-4o")
        {
            Self::O200kBase
        } else {
            Self::Cl100kBase
        }
    }
}

fn get_cl100k() -> &'static CoreBPE {
    CL100K.get_or_init(|| {
        tiktoken_rs::cl100k_base().expect("cl100k_base encoding should be available")
    })
}

fn get_o200k() -> &'static CoreBPE {
    O200K
        .get_or_init(|| tiktoken_rs::o200k_base().expect("o200k_base encoding should be available"))
}

/// Count tokens accurately using BPE tokenization.
///
/// Uses the specified encoding to produce an exact token count.
pub fn count_tokens(text: &str, encoding: TokenEncoding) -> usize {
    if text.is_empty() {
        return 0;
    }
    match encoding {
        TokenEncoding::Cl100kBase => get_cl100k().encode_with_special_tokens(text).len(),
        TokenEncoding::O200kBase => get_o200k().encode_with_special_tokens(text).len(),
    }
}

/// Count tokens using cl100k_base (the default for most models).
///
/// This is the primary entry point for token counting when the model
/// is unknown or when a model-agnostic count is needed.
pub fn count_tokens_default(text: &str) -> usize {
    count_tokens(text, TokenEncoding::Cl100kBase)
}

/// Count tokens for a specific model name.
///
/// Automatically selects the appropriate BPE encoding (cl100k_base or
/// o200k_base) based on the model name.
pub fn count_tokens_for_model(text: &str, model: &str) -> usize {
    count_tokens(text, TokenEncoding::for_model(model))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn count_empty() {
        assert_eq!(count_tokens_default(""), 0);
    }

    #[test]
    fn count_hello_world() {
        // "hello world" is 2 tokens in cl100k_base
        let count = count_tokens_default("hello world");
        assert_eq!(count, 2);
    }

    #[test]
    fn count_code_snippet() {
        let code = r#"fn main() { println!("hello"); }"#;
        let count = count_tokens_default(code);
        // BPE should produce a precise count; verify it's reasonable
        assert!(
            (8..=15).contains(&count),
            "code token count {count} out of expected range"
        );
    }

    #[test]
    fn count_long_text() {
        let text = "The quick brown fox jumps over the lazy dog. ".repeat(100);
        let count = count_tokens_default(&text);
        // ~10 tokens per sentence, 100 repetitions
        assert!(
            (900..=1100).contains(&count),
            "long text count {count} out of range"
        );
    }

    #[test]
    fn encoding_selection() {
        assert_eq!(
            TokenEncoding::for_model("claude-sonnet-4-20250514"),
            TokenEncoding::Cl100kBase
        );
        assert_eq!(TokenEncoding::for_model("gpt-4o"), TokenEncoding::O200kBase);
        assert_eq!(
            TokenEncoding::for_model("gpt-4o-mini"),
            TokenEncoding::O200kBase
        );
        assert_eq!(
            TokenEncoding::for_model("o3-mini"),
            TokenEncoding::O200kBase
        );
        assert_eq!(
            TokenEncoding::for_model("o4-mini"),
            TokenEncoding::O200kBase
        );
        assert_eq!(TokenEncoding::for_model("gpt-4"), TokenEncoding::Cl100kBase);
        assert_eq!(
            TokenEncoding::for_model("gemini-2.0-flash"),
            TokenEncoding::Cl100kBase
        );
        assert_eq!(
            TokenEncoding::for_model("llama-3"),
            TokenEncoding::Cl100kBase
        );
        assert_eq!(
            TokenEncoding::for_model("gpt-4.1-mini"),
            TokenEncoding::O200kBase
        );
    }

    #[test]
    fn o200k_vs_cl100k_both_reasonable() {
        let text = "Hello, how are you doing today?";
        let cl100k = count_tokens(text, TokenEncoding::Cl100kBase);
        let o200k = count_tokens(text, TokenEncoding::O200kBase);
        // Both should be reasonable
        assert!((5..=10).contains(&cl100k), "cl100k count {cl100k}");
        assert!((5..=10).contains(&o200k), "o200k count {o200k}");
    }

    #[test]
    fn model_aware_counting() {
        let text = "function hello() { return 'world'; }";
        let claude_count = count_tokens_for_model(text, "claude-sonnet-4-20250514");
        let gpt4o_count = count_tokens_for_model(text, "gpt-4o");
        // Both should be reasonable token counts
        assert!(claude_count > 0);
        assert!(gpt4o_count > 0);
    }

    #[test]
    fn accuracy_vs_old_heuristic() {
        // Demonstrate the improvement over the old heuristics
        let samples = vec![
            "hello world",
            "fn main() { println!(\"hello\"); }",
            "The quick brown fox jumps over the lazy dog",
            "{\n  \"key\": \"value\",\n  \"count\": 42\n}",
            "https://example.com/path?query=value&other=123",
        ];

        for text in &samples {
            let accurate = count_tokens_default(text);
            let old_word = {
                let wc = text.split_whitespace().count();
                if wc == 0 {
                    (text.len() / 4).max(1)
                } else {
                    (wc * 4 / 3).max(1)
                }
            };
            let old_char = (text.chars().count() / 4).max(1);

            // The accurate count should be > 0 for non-empty text
            assert!(accurate > 0, "accurate count for '{text}' should be > 0");

            // Log for manual inspection (visible in cargo test -- --nocapture)
            eprintln!(
                "  text={text:50} | accurate={accurate:3} | word_heuristic={old_word:3} | char_heuristic={old_char:3}"
            );
        }
    }
}
