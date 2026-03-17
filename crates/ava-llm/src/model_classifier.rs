//! Multi-strategy model classifier for per-request model selection.
//!
//! Inspired by Gemini CLI's model routing, this module selects the optimal
//! model per-request based on task complexity. Strategies are evaluated in
//! priority order (lower number = higher priority); the first strategy that
//! returns a decision wins.

/// Context provided to each routing strategy for classification.
#[derive(Debug, Clone)]
pub struct RoutingContext<'a> {
    /// The latest user message text.
    pub user_message: &'a str,
    /// Number of messages in the conversation so far.
    pub message_count: usize,
    /// Whether tool calls are present in the conversation.
    pub has_tool_calls: bool,
    /// The currently configured model identifier.
    pub current_model: &'a str,
}

/// A routing decision: which model to use and why.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RoutingDecision {
    /// Model identifier (e.g. "claude-haiku-4.5").
    pub model: String,
    /// Human-readable reason for this selection.
    pub reason: String,
}

/// Trait for pluggable model-selection strategies.
///
/// Strategies are tried in priority order (lower = higher priority).
/// Return `Some(decision)` to claim the routing, or `None` to defer
/// to the next strategy.
pub trait RoutingStrategy: Send + Sync {
    /// Human-readable name of this strategy.
    fn name(&self) -> &str;

    /// Priority value — lower numbers run first.
    fn priority(&self) -> u32;

    /// Evaluate the context and optionally return a routing decision.
    fn route(&self, context: &RoutingContext<'_>) -> Option<RoutingDecision>;
}

// ---------------------------------------------------------------------------
// Built-in strategies
// ---------------------------------------------------------------------------

/// Always returns a specific model when set (e.g. from `/model` command).
/// Priority 0 — takes absolute precedence.
pub struct OverrideStrategy {
    model: Option<String>,
}

impl OverrideStrategy {
    pub fn new(model: Option<String>) -> Self {
        Self { model }
    }

    /// Update the override model at runtime.
    pub fn set_model(&mut self, model: Option<String>) {
        self.model = model;
    }

    /// Return the currently configured override, if any.
    pub fn model(&self) -> Option<&str> {
        self.model.as_deref()
    }
}

impl RoutingStrategy for OverrideStrategy {
    fn name(&self) -> &str {
        "override"
    }

    fn priority(&self) -> u32 {
        0
    }

    fn route(&self, _context: &RoutingContext<'_>) -> Option<RoutingDecision> {
        self.model.as_ref().map(|m| RoutingDecision {
            model: m.clone(),
            reason: "manual model override".to_string(),
        })
    }
}

/// Heuristic complexity classifier that buckets requests into
/// simple / medium / complex and maps each to a model tier.
pub struct ComplexityClassifier {
    /// Model used for simple requests (greetings, short questions).
    pub cheap_model: String,
    /// Model used for typical coding tasks.
    pub default_model: String,
    /// Model used for complex, multi-step, or architectural tasks.
    pub frontier_model: String,
}

/// Complexity tier produced by the classifier heuristics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ComplexityTier {
    Simple,
    Medium,
    Complex,
}

impl ComplexityClassifier {
    pub fn new(
        cheap: impl Into<String>,
        default: impl Into<String>,
        frontier: impl Into<String>,
    ) -> Self {
        Self {
            cheap_model: cheap.into(),
            default_model: default.into(),
            frontier_model: frontier.into(),
        }
    }

    /// Classify a user message into a complexity tier.
    pub fn classify(&self, message: &str) -> ComplexityTier {
        let trimmed = message.trim();
        let len = trimmed.len();

        // Complex signals — checked first so they take precedence.
        if len > 500 {
            return ComplexityTier::Complex;
        }

        let lower = trimmed.to_lowercase();

        // Complex keywords / patterns
        const COMPLEX_KEYWORDS: &[&str] = &[
            "refactor",
            "architect",
            "redesign",
            "migrate",
            "rewrite",
            "implement a system",
            "design a",
            "build a framework",
            "multi-step",
            "step by step",
            "across all files",
            "entire codebase",
            "end-to-end",
            "comprehensive",
        ];
        if COMPLEX_KEYWORDS.iter().any(|kw| lower.contains(kw)) {
            return ComplexityTier::Complex;
        }

        // Simple signals
        if len < 100 {
            // Greeting patterns
            const GREETING_PATTERNS: &[&str] = &[
                "hi",
                "hello",
                "hey",
                "thanks",
                "thank you",
                "ok",
                "yes",
                "no",
                "sure",
                "got it",
                "good",
                "great",
                "bye",
                "goodbye",
            ];
            if GREETING_PATTERNS
                .iter()
                .any(|g| lower == *g || lower.starts_with(&format!("{g} ")))
            {
                return ComplexityTier::Simple;
            }

            // Simple question patterns (starts with question word, short)
            const QUESTION_STARTS: &[&str] = &[
                "what is",
                "what's",
                "how do i",
                "where is",
                "who is",
                "when did",
                "can you",
                "could you",
                "is there",
                "are there",
                "does ",
                "do you",
                "why is",
                "why does",
            ];
            if QUESTION_STARTS.iter().any(|q| lower.starts_with(q)) {
                return ComplexityTier::Simple;
            }
        }

        // Medium signals — code-related keywords, file operations
        const MEDIUM_KEYWORDS: &[&str] = &[
            "fix", "bug", "error", "add", "create", "update", "change", "modify", "edit", "delete",
            "remove", "rename", "move", "function", "method", "class", "struct", "trait", "impl",
            "test", "compile", "build", "run", "debug", "lint", "file", "module", "crate",
            "package", "import", "read", "write", "parse", "format",
        ];
        if MEDIUM_KEYWORDS.iter().any(|kw| lower.contains(kw)) {
            return ComplexityTier::Medium;
        }

        // Default: medium for anything that didn't match simple or complex
        ComplexityTier::Medium
    }
}

impl RoutingStrategy for ComplexityClassifier {
    fn name(&self) -> &str {
        "complexity-classifier"
    }

    fn priority(&self) -> u32 {
        50
    }

    fn route(&self, context: &RoutingContext<'_>) -> Option<RoutingDecision> {
        let tier = self.classify(context.user_message);
        let (model, reason) = match tier {
            ComplexityTier::Simple => (&self.cheap_model, "simple request — routed to cheap model"),
            ComplexityTier::Medium => (
                &self.default_model,
                "standard coding task — routed to default model",
            ),
            ComplexityTier::Complex => (
                &self.frontier_model,
                "complex/multi-step task — routed to frontier model",
            ),
        };
        Some(RoutingDecision {
            model: model.clone(),
            reason: reason.to_string(),
        })
    }
}

/// Last-resort strategy that returns the currently configured model.
/// Priority 100.
pub struct FallbackStrategy;

impl RoutingStrategy for FallbackStrategy {
    fn name(&self) -> &str {
        "fallback"
    }

    fn priority(&self) -> u32 {
        100
    }

    fn route(&self, context: &RoutingContext<'_>) -> Option<RoutingDecision> {
        Some(RoutingDecision {
            model: context.current_model.to_string(),
            reason: "fallback — keeping current model".to_string(),
        })
    }
}

// ---------------------------------------------------------------------------
// Classifier router
// ---------------------------------------------------------------------------

/// Multi-strategy classifier router.
///
/// Strategies are sorted by priority (ascending) and evaluated in order.
/// The first `Some(RoutingDecision)` wins.
pub struct ClassifierRouter {
    strategies: Vec<Box<dyn RoutingStrategy>>,
}

impl ClassifierRouter {
    /// Create an empty router with no strategies.
    pub fn new() -> Self {
        Self {
            strategies: Vec::new(),
        }
    }

    /// Create a router pre-loaded with the three default strategies:
    /// `OverrideStrategy` (no override set), `ComplexityClassifier`, and `FallbackStrategy`.
    pub fn new_with_defaults(cheap: &str, default: &str, frontier: &str) -> Self {
        let mut router = Self::new();
        router.add_strategy(Box::new(OverrideStrategy::new(None)));
        router.add_strategy(Box::new(ComplexityClassifier::new(
            cheap, default, frontier,
        )));
        router.add_strategy(Box::new(FallbackStrategy));
        router
    }

    /// Add a strategy. The internal list is re-sorted by priority after insertion.
    pub fn add_strategy(&mut self, strategy: Box<dyn RoutingStrategy>) {
        self.strategies.push(strategy);
        self.strategies.sort_by_key(|s| s.priority());
    }

    /// Evaluate strategies in priority order and return the first decision.
    ///
    /// Because `FallbackStrategy` always returns `Some`, this will never
    /// return a "no decision" if the defaults are loaded.  If no strategy
    /// matches, a synthetic fallback using the current model is returned.
    pub fn route(&self, context: &RoutingContext<'_>) -> RoutingDecision {
        for strategy in &self.strategies {
            if let Some(decision) = strategy.route(context) {
                return decision;
            }
        }
        // Safety net — should be unreachable when FallbackStrategy is present.
        RoutingDecision {
            model: context.current_model.to_string(),
            reason: "no strategy matched — using current model".to_string(),
        }
    }

    /// Return the number of registered strategies.
    pub fn strategy_count(&self) -> usize {
        self.strategies.len()
    }
}

impl Default for ClassifierRouter {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    const CHEAP: &str = "claude-haiku-4.5";
    const DEFAULT: &str = "claude-sonnet-4.6";
    const FRONTIER: &str = "claude-opus-4.5";

    fn ctx(message: &str) -> RoutingContext<'_> {
        RoutingContext {
            user_message: message,
            message_count: 1,
            has_tool_calls: false,
            current_model: DEFAULT,
        }
    }

    // -- OverrideStrategy --------------------------------------------------

    #[test]
    fn override_takes_priority() {
        let mut router = ClassifierRouter::new_with_defaults(CHEAP, DEFAULT, FRONTIER);
        // Default override is None, so complexity classifier decides.
        let decision = router.route(&ctx("hello"));
        assert_eq!(decision.model, CHEAP);

        // Now set an override — it should win regardless of message.
        router.strategies.clear();
        router.add_strategy(Box::new(OverrideStrategy::new(Some(
            "custom-model".to_string(),
        ))));
        router.add_strategy(Box::new(ComplexityClassifier::new(
            CHEAP, DEFAULT, FRONTIER,
        )));
        router.add_strategy(Box::new(FallbackStrategy));

        let decision = router.route(&ctx("refactor the entire codebase"));
        assert_eq!(decision.model, "custom-model");
        assert!(decision.reason.contains("override"));
    }

    #[test]
    fn override_none_defers_to_next_strategy() {
        let strategy = OverrideStrategy::new(None);
        assert!(strategy.route(&ctx("anything")).is_none());
    }

    // -- ComplexityClassifier ----------------------------------------------

    #[test]
    fn classifies_simple_greetings() {
        let classifier = ComplexityClassifier::new(CHEAP, DEFAULT, FRONTIER);
        assert_eq!(classifier.classify("hello"), ComplexityTier::Simple);
        assert_eq!(classifier.classify("hi"), ComplexityTier::Simple);
        assert_eq!(classifier.classify("thanks"), ComplexityTier::Simple);
        assert_eq!(classifier.classify("ok"), ComplexityTier::Simple);
    }

    #[test]
    fn classifies_simple_questions() {
        let classifier = ComplexityClassifier::new(CHEAP, DEFAULT, FRONTIER);
        assert_eq!(
            classifier.classify("what is a mutex?"),
            ComplexityTier::Simple
        );
        assert_eq!(
            classifier.classify("how do i install rust?"),
            ComplexityTier::Simple
        );
    }

    #[test]
    fn classifies_medium_code_tasks() {
        let classifier = ComplexityClassifier::new(CHEAP, DEFAULT, FRONTIER);
        assert_eq!(
            classifier.classify("fix the bug in the parser module"),
            ComplexityTier::Medium
        );
        assert_eq!(
            classifier.classify("add a new test for the router"),
            ComplexityTier::Medium
        );
        assert_eq!(
            classifier.classify("create a struct for configuration"),
            ComplexityTier::Medium
        );
    }

    #[test]
    fn classifies_complex_by_keywords() {
        let classifier = ComplexityClassifier::new(CHEAP, DEFAULT, FRONTIER);
        assert_eq!(
            classifier.classify("refactor the authentication system"),
            ComplexityTier::Complex
        );
        assert_eq!(
            classifier.classify("architect a new plugin system"),
            ComplexityTier::Complex
        );
        assert_eq!(
            classifier.classify("migrate the database layer to async"),
            ComplexityTier::Complex
        );
        assert_eq!(
            classifier.classify("redesign the entire pipeline"),
            ComplexityTier::Complex
        );
    }

    #[test]
    fn classifies_complex_by_length() {
        let classifier = ComplexityClassifier::new(CHEAP, DEFAULT, FRONTIER);
        let long_message = "a".repeat(501);
        assert_eq!(classifier.classify(&long_message), ComplexityTier::Complex);
    }

    #[test]
    fn routes_simple_to_cheap_model() {
        let router = ClassifierRouter::new_with_defaults(CHEAP, DEFAULT, FRONTIER);
        let decision = router.route(&ctx("hello"));
        assert_eq!(decision.model, CHEAP);
        assert!(decision.reason.contains("cheap"));
    }

    #[test]
    fn routes_medium_to_default_model() {
        let router = ClassifierRouter::new_with_defaults(CHEAP, DEFAULT, FRONTIER);
        let decision = router.route(&ctx("fix the bug in the parser module"));
        assert_eq!(decision.model, DEFAULT);
        assert!(decision.reason.contains("default"));
    }

    #[test]
    fn routes_complex_to_frontier_model() {
        let router = ClassifierRouter::new_with_defaults(CHEAP, DEFAULT, FRONTIER);
        let decision = router.route(&ctx("refactor the entire authentication system"));
        assert_eq!(decision.model, FRONTIER);
        assert!(decision.reason.contains("frontier"));
    }

    // -- FallbackStrategy --------------------------------------------------

    #[test]
    fn fallback_returns_current_model() {
        let strategy = FallbackStrategy;
        let context = ctx("anything");
        let decision = strategy.route(&context).unwrap();
        assert_eq!(decision.model, DEFAULT);
        assert!(decision.reason.contains("fallback"));
    }

    // -- Strategy ordering -------------------------------------------------

    #[test]
    fn strategies_sorted_by_priority() {
        let router = ClassifierRouter::new_with_defaults(CHEAP, DEFAULT, FRONTIER);
        let priorities: Vec<u32> = router.strategies.iter().map(|s| s.priority()).collect();
        assert_eq!(priorities, vec![0, 50, 100]);
    }

    #[test]
    fn custom_strategy_inserted_in_correct_order() {
        struct MidStrategy;
        impl RoutingStrategy for MidStrategy {
            fn name(&self) -> &str {
                "mid"
            }
            fn priority(&self) -> u32 {
                25
            }
            fn route(&self, _ctx: &RoutingContext<'_>) -> Option<RoutingDecision> {
                None
            }
        }

        let mut router = ClassifierRouter::new_with_defaults(CHEAP, DEFAULT, FRONTIER);
        router.add_strategy(Box::new(MidStrategy));
        let priorities: Vec<u32> = router.strategies.iter().map(|s| s.priority()).collect();
        assert_eq!(priorities, vec![0, 25, 50, 100]);
    }

    // -- Empty router safety net -------------------------------------------

    #[test]
    fn empty_router_returns_current_model() {
        let router = ClassifierRouter::new();
        let decision = router.route(&ctx("hello"));
        assert_eq!(decision.model, DEFAULT);
        assert!(decision.reason.contains("no strategy matched"));
    }

    // -- OverrideStrategy mutability ---------------------------------------

    #[test]
    fn override_set_and_clear() {
        let mut strategy = OverrideStrategy::new(None);
        assert!(strategy.model().is_none());

        strategy.set_model(Some("gpt-5".to_string()));
        assert_eq!(strategy.model(), Some("gpt-5"));

        let decision = strategy.route(&ctx("anything")).unwrap();
        assert_eq!(decision.model, "gpt-5");

        strategy.set_model(None);
        assert!(strategy.route(&ctx("anything")).is_none());
    }
}
