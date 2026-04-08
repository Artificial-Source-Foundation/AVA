//! Continuation detection — tracks consecutive agent turns and detects
//! diminishing returns (repetitive output across turns).
//!
//! When the agent produces substantially similar content across multiple
//! consecutive turns, it is likely stuck in a loop and continuing will
//! waste tokens without making progress.

use std::collections::HashSet;

/// Tracks consecutive continuation turns and detects diminishing returns.
#[derive(Debug, Clone)]
pub struct ContinuationTracker {
    /// Hash of the 4-gram set from the previous turn's content.
    previous_content_hash: Option<u64>,
    /// Set of 4-grams from the previous turn's content (for similarity comparison).
    previous_ngrams: Option<HashSet<u64>>,
    /// Number of turns since the tracker was created or reset.
    pub continuation_count: u32,
    /// Number of consecutive turns where content was highly similar.
    repetition_count: u32,
    /// Set to true after 2+ consecutive repetitive continuations.
    pub diminishing_returns_detected: bool,
}

impl Default for ContinuationTracker {
    fn default() -> Self {
        Self::new()
    }
}

impl ContinuationTracker {
    pub fn new() -> Self {
        Self {
            previous_content_hash: None,
            previous_ngrams: None,
            continuation_count: 0,
            repetition_count: 0,
            diminishing_returns_detected: false,
        }
    }

    /// Observe a new turn's content. Returns `true` if diminishing returns
    /// were detected (caller should stop continuing).
    pub fn observe(&mut self, content: &str) -> bool {
        self.continuation_count += 1;

        let ngrams = extract_ngrams(content, 4);
        let content_hash = hash_ngram_set(&ngrams);

        if let (Some(prev_hash), Some(ref prev_ngrams)) =
            (self.previous_content_hash, &self.previous_ngrams)
        {
            if prev_hash == content_hash {
                // Identical content
                self.repetition_count += 1;
            } else {
                let similarity = ngram_similarity(prev_ngrams, &ngrams);
                if similarity > 0.80 {
                    self.repetition_count += 1;
                } else {
                    // Novel content — reset repetition counter
                    self.repetition_count = 0;
                }
            }
        }

        self.previous_content_hash = Some(content_hash);
        self.previous_ngrams = Some(ngrams);

        if self.repetition_count >= 2 {
            self.diminishing_returns_detected = true;
            tracing::info!(
                continuation_count = self.continuation_count,
                repetition_count = self.repetition_count,
                "F29: stopping continuation — diminishing returns detected"
            );
            return true;
        }

        false
    }

    /// Reset the tracker (e.g., after a steering message or compaction).
    pub fn reset(&mut self) {
        self.previous_content_hash = None;
        self.previous_ngrams = None;
        self.continuation_count = 0;
        self.repetition_count = 0;
        self.diminishing_returns_detected = false;
    }
}

/// Extract character-level n-grams from text, returning their hashes.
fn extract_ngrams(text: &str, n: usize) -> HashSet<u64> {
    use std::hash::{Hash, Hasher};

    let chars: Vec<char> = text.chars().collect();
    if chars.len() < n {
        let mut set = HashSet::new();
        let mut hasher = std::hash::DefaultHasher::new();
        chars.hash(&mut hasher);
        set.insert(hasher.finish());
        return set;
    }

    let mut set = HashSet::with_capacity(chars.len().saturating_sub(n - 1));
    for window in chars.windows(n) {
        let mut hasher = std::hash::DefaultHasher::new();
        window.hash(&mut hasher);
        set.insert(hasher.finish());
    }
    set
}

/// Compute a single hash from a set of n-gram hashes (order-independent).
fn hash_ngram_set(ngrams: &HashSet<u64>) -> u64 {
    // XOR is commutative and associative — order-independent.
    ngrams.iter().fold(0u64, |acc, &h| acc ^ h)
}

/// Compute Jaccard similarity between two n-gram sets.
fn ngram_similarity(a: &HashSet<u64>, b: &HashSet<u64>) -> f64 {
    if a.is_empty() && b.is_empty() {
        return 1.0;
    }
    let intersection = a.intersection(b).count();
    let union = a.union(b).count();
    if union == 0 {
        return 1.0;
    }
    intersection as f64 / union as f64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identical_content_detected_as_diminishing() {
        let mut tracker = ContinuationTracker::new();
        let content = "The quick brown fox jumps over the lazy dog";

        // First observation — no previous to compare
        assert!(!tracker.observe(content));
        assert_eq!(tracker.repetition_count, 0);

        // Second — identical, repetition_count = 1
        assert!(!tracker.observe(content));
        assert_eq!(tracker.repetition_count, 1);

        // Third — identical again, repetition_count = 2 -> diminishing
        assert!(tracker.observe(content));
        assert!(tracker.diminishing_returns_detected);
    }

    #[test]
    fn different_content_allows_continuation() {
        let mut tracker = ContinuationTracker::new();

        assert!(!tracker.observe("First turn: implementing the user interface module"));
        assert!(!tracker.observe("Second turn: now testing the database integration layer"));
        assert!(!tracker.observe("Third turn: fixing the authentication middleware bugs"));
        assert!(!tracker.diminishing_returns_detected);
        assert_eq!(tracker.repetition_count, 0);
    }

    #[test]
    fn counter_resets_on_novel_content() {
        let mut tracker = ContinuationTracker::new();
        let content = "Repeating this exact same content over and over again";

        assert!(!tracker.observe(content));
        assert!(!tracker.observe(content)); // repetition = 1

        // Novel content breaks the streak
        assert!(!tracker.observe("Completely different content about a new topic entirely"));
        assert_eq!(tracker.repetition_count, 0);

        // Need 2 more repetitions to trigger again
        let new_content = "Another repeated message that keeps showing up";
        assert!(!tracker.observe(new_content));
        assert!(!tracker.observe(new_content)); // repetition = 1
        assert!(tracker.observe(new_content)); // repetition = 2 -> diminishing
    }

    #[test]
    fn highly_similar_content_counts_as_repetition() {
        let mut tracker = ContinuationTracker::new();
        // Only minor differences (changing a word or two)
        let base =
            "I am going to read the file and then edit the contents to fix the bug in the function";
        let similar =
            "I am going to read the file and then edit the contents to fix the bug in the method";

        assert!(!tracker.observe(base));
        assert!(!tracker.observe(similar));
        // High similarity should count as repetition
        assert_eq!(tracker.repetition_count, 1);
    }

    #[test]
    fn reset_clears_all_state() {
        let mut tracker = ContinuationTracker::new();
        let content = "Some content that is used for testing the tracker";

        tracker.observe(content);
        tracker.observe(content);
        assert_eq!(tracker.repetition_count, 1);

        tracker.reset();
        assert_eq!(tracker.continuation_count, 0);
        assert_eq!(tracker.repetition_count, 0);
        assert!(!tracker.diminishing_returns_detected);
        assert!(tracker.previous_content_hash.is_none());
    }

    #[test]
    fn short_content_handled_gracefully() {
        let mut tracker = ContinuationTracker::new();
        assert!(!tracker.observe("hi"));
        assert!(!tracker.observe("hi"));
        assert!(tracker.observe("hi"));
        assert!(tracker.diminishing_returns_detected);
    }

    #[test]
    fn empty_content_handled_gracefully() {
        let mut tracker = ContinuationTracker::new();
        assert!(!tracker.observe(""));
        assert!(!tracker.observe(""));
        assert!(tracker.observe(""));
    }

    #[test]
    fn ngram_similarity_identical_sets() {
        let a = extract_ngrams("hello world test", 4);
        let b = extract_ngrams("hello world test", 4);
        assert!((ngram_similarity(&a, &b) - 1.0).abs() < f64::EPSILON);
    }

    #[test]
    fn ngram_similarity_disjoint_sets() {
        let a = extract_ngrams("aaaa bbbb cccc", 4);
        let b = extract_ngrams("xxxx yyyy zzzz", 4);
        assert!(ngram_similarity(&a, &b) < 0.1);
    }
}
