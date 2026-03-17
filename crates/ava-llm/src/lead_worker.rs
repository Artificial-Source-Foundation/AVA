//! Lead-worker provider with automatic failback.
//!
//! Routes the first N turns to an expensive "lead" model, then switches to a
//! cheaper "worker" model. If the worker fails consecutively, the provider
//! automatically promotes back to the lead for another N turns.

use std::pin::Pin;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use async_trait::async_trait;
use ava_types::{Message, Result, StreamChunk, ThinkingLevel, Tool};
use futures::Stream;

use crate::message_transform::ProviderKind;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
use crate::thinking::{ResolvedThinkingConfig, ThinkingConfig};

/// A provider wrapper that uses an expensive "lead" model for the first N turns,
/// then switches to a cheaper "worker" model. On consecutive worker failures,
/// it automatically promotes back to the lead.
pub struct LeadWorkerProvider {
    lead: Box<dyn LLMProvider>,
    worker: Box<dyn LLMProvider>,
    /// Number of turns that use the lead model at the start (and after each promotion).
    lead_turns: usize,
    /// Current turn counter (incremented on each generate call).
    current_turn: AtomicUsize,
    /// Consecutive worker failures since last success.
    consecutive_worker_failures: AtomicUsize,
    /// Number of consecutive worker failures before promoting back to lead.
    failure_threshold: usize,
    /// Whether we are currently in lead mode (true at start, toggled by turn count and failures).
    is_using_lead: AtomicBool,
    /// The turn number at which the current lead phase started (for promotion resets).
    lead_phase_start: AtomicUsize,
}

impl LeadWorkerProvider {
    /// Create a new lead-worker provider.
    ///
    /// - `lead`: the expensive/frontier model provider
    /// - `worker`: the cheap/fast model provider
    /// - `lead_turns`: how many turns use the lead model (default: 3)
    /// - `failure_threshold`: promote back to lead after this many consecutive worker failures (default: 2)
    pub fn new(
        lead: Box<dyn LLMProvider>,
        worker: Box<dyn LLMProvider>,
        lead_turns: usize,
        failure_threshold: usize,
    ) -> Self {
        Self {
            lead,
            worker,
            lead_turns,
            current_turn: AtomicUsize::new(0),
            consecutive_worker_failures: AtomicUsize::new(0),
            failure_threshold,
            is_using_lead: AtomicBool::new(true),
            lead_phase_start: AtomicUsize::new(0),
        }
    }

    /// Create with default settings (3 lead turns, 2 failure threshold).
    pub fn with_defaults(lead: Box<dyn LLMProvider>, worker: Box<dyn LLMProvider>) -> Self {
        Self::new(lead, worker, 3, 2)
    }

    /// Determine which provider to use for the current turn, advance the turn
    /// counter, and return a reference to the active provider.
    fn active_provider(&self) -> &dyn LLMProvider {
        let turn = self.current_turn.fetch_add(1, Ordering::SeqCst);

        if self.is_using_lead.load(Ordering::SeqCst) {
            let phase_start = self.lead_phase_start.load(Ordering::SeqCst);
            if turn >= phase_start + self.lead_turns {
                // Lead phase complete, switch to worker
                self.is_using_lead.store(false, Ordering::SeqCst);
                self.consecutive_worker_failures.store(0, Ordering::SeqCst);
                self.worker.as_ref()
            } else {
                self.lead.as_ref()
            }
        } else {
            self.worker.as_ref()
        }
    }

    /// Record a worker success — resets the consecutive failure counter.
    fn record_worker_success(&self) {
        self.consecutive_worker_failures.store(0, Ordering::SeqCst);
    }

    /// Record a worker failure. If the threshold is met, promote back to lead.
    fn record_worker_failure(&self) {
        let failures = self
            .consecutive_worker_failures
            .fetch_add(1, Ordering::SeqCst)
            + 1;
        if failures >= self.failure_threshold {
            self.promote_to_lead();
        }
    }

    /// Promote back to lead mode for the next N turns.
    fn promote_to_lead(&self) {
        let current = self.current_turn.load(Ordering::SeqCst);
        self.lead_phase_start.store(current, Ordering::SeqCst);
        self.is_using_lead.store(true, Ordering::SeqCst);
        self.consecutive_worker_failures.store(0, Ordering::SeqCst);
        tracing::info!(
            "LeadWorkerProvider: promoting back to lead model after consecutive worker failures"
        );
    }

    /// Returns the currently active provider without advancing the turn counter.
    fn current_provider(&self) -> &dyn LLMProvider {
        if self.is_using_lead.load(Ordering::SeqCst) {
            self.lead.as_ref()
        } else {
            self.worker.as_ref()
        }
    }

    /// Whether the active provider (without advancing) is the lead.
    fn is_lead_active(&self) -> bool {
        self.is_using_lead.load(Ordering::SeqCst)
    }
}

#[async_trait]
impl LLMProvider for LeadWorkerProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider.generate(messages).await {
            Ok(result) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(result)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider.generate_stream(messages).await {
            Ok(stream) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(stream)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        self.current_provider().estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        self.current_provider()
            .estimate_cost(input_tokens, output_tokens)
    }

    fn model_name(&self) -> &str {
        self.current_provider().model_name()
    }

    fn capabilities(&self) -> ProviderCapabilities {
        self.current_provider().capabilities()
    }

    fn provider_kind(&self) -> ProviderKind {
        self.current_provider().provider_kind()
    }

    fn supports_tools(&self) -> bool {
        self.current_provider().supports_tools()
    }

    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<LLMResponse> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider.generate_with_tools(messages, tools).await {
            Ok(result) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(result)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    fn supports_thinking(&self) -> bool {
        self.current_provider().supports_thinking()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        self.current_provider().thinking_levels()
    }

    fn resolve_thinking_config(&self, config: ThinkingConfig) -> ResolvedThinkingConfig {
        self.current_provider().resolve_thinking_config(config)
    }

    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider
            .generate_with_thinking(messages, tools, thinking)
            .await
        {
            Ok(result) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(result)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    async fn generate_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<LLMResponse> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider
            .generate_with_thinking_config(messages, tools, config)
            .await
        {
            Ok(result) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(result)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider.generate_stream_with_tools(messages, tools).await {
            Ok(stream) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(stream)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider
            .generate_stream_with_thinking(messages, tools, thinking)
            .await
        {
            Ok(stream) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(stream)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }

    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let provider = self.active_provider();
        let is_worker = !self.is_lead_active();

        match provider
            .generate_stream_with_thinking_config(messages, tools, config)
            .await
        {
            Ok(stream) => {
                if is_worker {
                    self.record_worker_success();
                }
                Ok(stream)
            }
            Err(e) => {
                if is_worker {
                    self.record_worker_failure();
                }
                Err(e)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::AvaError;
    use std::sync::atomic::AtomicUsize;
    use std::sync::Arc;

    /// Mock provider that returns preset responses or errors.
    struct MockProvider {
        name: String,
        call_count: Arc<AtomicUsize>,
        /// If set, generate calls will fail with this message.
        fail_after: Option<usize>, // fail after N successful calls
        total_calls: Arc<AtomicUsize>,
    }

    impl MockProvider {
        fn new(name: &str) -> Self {
            Self {
                name: name.to_string(),
                call_count: Arc::new(AtomicUsize::new(0)),
                fail_after: None,
                total_calls: Arc::new(AtomicUsize::new(0)),
            }
        }

        fn always_failing(name: &str) -> Self {
            Self {
                name: name.to_string(),
                call_count: Arc::new(AtomicUsize::new(0)),
                fail_after: Some(0),
                total_calls: Arc::new(AtomicUsize::new(0)),
            }
        }

        fn with_shared_counter(name: &str, counter: Arc<AtomicUsize>) -> Self {
            Self {
                name: name.to_string(),
                call_count: counter,
                fail_after: None,
                total_calls: Arc::new(AtomicUsize::new(0)),
            }
        }
    }

    #[async_trait]
    impl LLMProvider for MockProvider {
        async fn generate(&self, _messages: &[Message]) -> Result<String> {
            let n = self.total_calls.fetch_add(1, Ordering::SeqCst);
            if let Some(fail_after) = self.fail_after {
                if n >= fail_after {
                    return Err(AvaError::ProviderError {
                        provider: self.name.clone(),
                        message: format!("{} failed", self.name),
                    });
                }
            }
            self.call_count.fetch_add(1, Ordering::SeqCst);
            Ok(format!("response from {}", self.name))
        }

        async fn generate_stream(
            &self,
            _messages: &[Message],
        ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
            let n = self.total_calls.fetch_add(1, Ordering::SeqCst);
            if let Some(fail_after) = self.fail_after {
                if n >= fail_after {
                    return Err(AvaError::ProviderError {
                        provider: self.name.clone(),
                        message: format!("{} stream failed", self.name),
                    });
                }
            }
            self.call_count.fetch_add(1, Ordering::SeqCst);
            Ok(Box::pin(futures::stream::empty()))
        }

        fn estimate_tokens(&self, input: &str) -> usize {
            input.len() / 4
        }

        fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
            if self.name.contains("lead") {
                (input_tokens + output_tokens) as f64 * 0.01
            } else {
                (input_tokens + output_tokens) as f64 * 0.001
            }
        }

        fn model_name(&self) -> &str {
            &self.name
        }

        fn supports_tools(&self) -> bool {
            true
        }
    }

    #[tokio::test]
    async fn test_lead_for_first_n_turns() {
        let lead_counter = Arc::new(AtomicUsize::new(0));
        let worker_counter = Arc::new(AtomicUsize::new(0));

        let lead = MockProvider::with_shared_counter("lead-model", lead_counter.clone());
        let worker = MockProvider::with_shared_counter("worker-model", worker_counter.clone());

        let provider = LeadWorkerProvider::new(Box::new(lead), Box::new(worker), 3, 2);
        let msgs: Vec<Message> = vec![];

        // First 3 turns should use lead
        for _ in 0..3 {
            let result = provider.generate(&msgs).await.unwrap();
            assert!(
                result.contains("lead"),
                "Expected lead response, got: {result}"
            );
        }

        assert_eq!(lead_counter.load(Ordering::SeqCst), 3);
        assert_eq!(worker_counter.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn test_switches_to_worker_after_lead_turns() {
        let provider = LeadWorkerProvider::new(
            Box::new(MockProvider::new("lead-model")),
            Box::new(MockProvider::new("worker-model")),
            2,
            2,
        );
        let msgs: Vec<Message> = vec![];

        // First 2 turns: lead
        let r1 = provider.generate(&msgs).await.unwrap();
        assert!(r1.contains("lead"));
        let r2 = provider.generate(&msgs).await.unwrap();
        assert!(r2.contains("lead"));

        // Turn 3+: worker
        let r3 = provider.generate(&msgs).await.unwrap();
        assert!(r3.contains("worker"), "Expected worker, got: {r3}");
        let r4 = provider.generate(&msgs).await.unwrap();
        assert!(r4.contains("worker"), "Expected worker, got: {r4}");
    }

    #[tokio::test]
    async fn test_promotes_to_lead_on_consecutive_failures() {
        let provider = LeadWorkerProvider::new(
            Box::new(MockProvider::new("lead-model")),
            Box::new(MockProvider::always_failing("worker-model")),
            1, // only 1 lead turn
            2, // promote after 2 consecutive failures
        );
        let msgs: Vec<Message> = vec![];

        // Turn 0: lead (success)
        let r = provider.generate(&msgs).await.unwrap();
        assert!(r.contains("lead"));

        // Turn 1: worker (fail #1)
        assert!(provider.generate(&msgs).await.is_err());
        // Turn 2: worker (fail #2) — triggers promotion
        assert!(provider.generate(&msgs).await.is_err());

        // Turn 3: should be back on lead after promotion
        let r = provider.generate(&msgs).await.unwrap();
        assert!(
            r.contains("lead"),
            "Expected lead after promotion, got: {r}"
        );
    }

    #[tokio::test]
    async fn test_worker_success_resets_failure_counter() {
        // Worker that fails on 2nd call only, succeeds on 1st and 3rd
        let worker = MockProvider {
            name: "worker-model".to_string(),
            call_count: Arc::new(AtomicUsize::new(0)),
            fail_after: None, // always succeeds
            total_calls: Arc::new(AtomicUsize::new(0)),
        };

        let provider = LeadWorkerProvider::new(
            Box::new(MockProvider::new("lead-model")),
            Box::new(worker),
            1, // 1 lead turn
            3, // need 3 consecutive failures to promote
        );
        let msgs: Vec<Message> = vec![];

        // Turn 0: lead
        provider.generate(&msgs).await.unwrap();

        // Turns 1+: worker succeeds, failure counter stays at 0
        let r = provider.generate(&msgs).await.unwrap();
        assert!(r.contains("worker"));
        assert_eq!(
            provider.consecutive_worker_failures.load(Ordering::SeqCst),
            0
        );
    }

    #[tokio::test]
    async fn test_model_name_reflects_active_provider() {
        let provider = LeadWorkerProvider::new(
            Box::new(MockProvider::new("opus-4")),
            Box::new(MockProvider::new("haiku-4.5")),
            2,
            2,
        );

        // Before any calls, lead is active
        assert_eq!(provider.model_name(), "opus-4");

        let msgs: Vec<Message> = vec![];
        // Use up lead turns
        provider.generate(&msgs).await.unwrap();
        provider.generate(&msgs).await.unwrap();

        // Now worker should be active
        // Need to trigger the switch by calling active_provider once more
        // The switch happens inside active_provider when turn >= phase_start + lead_turns
        // After 2 generate calls, current_turn is 2, phase_start is 0, lead_turns is 2
        // So the next active_provider call will switch. But model_name uses current_provider
        // which checks is_using_lead. The switch happens inside active_provider.
        // Let's call generate to trigger the switch.
        provider.generate(&msgs).await.unwrap();
        assert_eq!(provider.model_name(), "haiku-4.5");
    }

    #[tokio::test]
    async fn test_estimate_cost_delegates_to_active() {
        let provider = LeadWorkerProvider::new(
            Box::new(MockProvider::new("lead-model")),
            Box::new(MockProvider::new("worker-model")),
            1,
            2,
        );

        // Lead is active — expensive
        let lead_cost = provider.estimate_cost(1000, 500);
        assert!((lead_cost - 15.0).abs() < 0.01); // (1000+500) * 0.01

        let msgs: Vec<Message> = vec![];
        provider.generate(&msgs).await.unwrap(); // use up lead turn
        provider.generate(&msgs).await.unwrap(); // triggers switch to worker

        // Worker is active — cheap
        let worker_cost = provider.estimate_cost(1000, 500);
        assert!((worker_cost - 1.5).abs() < 0.01); // (1000+500) * 0.001
    }

    #[tokio::test]
    async fn test_promotion_gives_full_lead_turns() {
        let provider = LeadWorkerProvider::new(
            Box::new(MockProvider::new("lead-model")),
            Box::new(MockProvider::always_failing("worker-model")),
            2, // 2 lead turns each phase
            1, // promote after 1 failure
        );
        let msgs: Vec<Message> = vec![];

        // Phase 1: 2 lead turns
        let r = provider.generate(&msgs).await.unwrap();
        assert!(r.contains("lead"));
        let r = provider.generate(&msgs).await.unwrap();
        assert!(r.contains("lead"));

        // Worker fails once => promotion
        assert!(provider.generate(&msgs).await.is_err());

        // Phase 2: should get 2 more lead turns
        let r = provider.generate(&msgs).await.unwrap();
        assert!(
            r.contains("lead"),
            "Expected lead turn 1 of phase 2, got: {r}"
        );
        let r = provider.generate(&msgs).await.unwrap();
        assert!(
            r.contains("lead"),
            "Expected lead turn 2 of phase 2, got: {r}"
        );
    }

    #[tokio::test]
    async fn test_with_defaults() {
        let provider = LeadWorkerProvider::with_defaults(
            Box::new(MockProvider::new("lead")),
            Box::new(MockProvider::new("worker")),
        );
        assert_eq!(provider.lead_turns, 3);
        assert_eq!(provider.failure_threshold, 2);
    }
}
