//! Hook system with lifecycle events for the agent loop.
//!
//! Provides a configurable hook infrastructure that fires at key lifecycle
//! points (session start/end, before/after model calls, before/after tool
//! execution). Hooks can observe, block, or modify agent behavior.
//!
//! Inspired by Gemini CLI's hook system but adapted for AVA's async Rust
//! architecture with parallel hook execution and priority-based outcome
//! aggregation.

use async_trait::async_trait;
use ava_types::{Message, ToolCall, ToolResult};
use std::sync::Arc;
use tokio::process::Command;

// ---------------------------------------------------------------------------
// HookEvent — lifecycle points where hooks can fire
// ---------------------------------------------------------------------------

/// Lifecycle events that hooks can subscribe to.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum HookEvent {
    /// A new agent session is starting.
    SessionStart,
    /// About to send messages to the LLM.
    BeforeModel,
    /// Received a response from the LLM.
    AfterModel,
    /// About to execute a tool call.
    BeforeToolExecution,
    /// A tool call has completed.
    AfterToolExecution,
    /// The agent session is ending.
    SessionEnd,
}

impl std::fmt::Display for HookEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::SessionStart => write!(f, "session_start"),
            Self::BeforeModel => write!(f, "before_model"),
            Self::AfterModel => write!(f, "after_model"),
            Self::BeforeToolExecution => write!(f, "before_tool_execution"),
            Self::AfterToolExecution => write!(f, "after_tool_execution"),
            Self::SessionEnd => write!(f, "session_end"),
        }
    }
}

// ---------------------------------------------------------------------------
// HookOutcome — what a hook wants to happen
// ---------------------------------------------------------------------------

/// The outcome of a hook execution.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum HookOutcome {
    /// Proceed normally — no interference.
    Continue,
    /// Block the action with the given reason.
    Block(String),
    /// Inject or modify context with the given message.
    Modify(String),
}

impl HookOutcome {
    /// Priority for aggregation: Block > Modify > Continue.
    #[cfg(test)]
    fn priority(&self) -> u8 {
        match self {
            Self::Block(_) => 2,
            Self::Modify(_) => 1,
            Self::Continue => 0,
        }
    }
}

// ---------------------------------------------------------------------------
// HookContext — data available to hooks at execution time
// ---------------------------------------------------------------------------

/// Context passed to a hook when it fires.
#[derive(Debug, Clone)]
pub struct HookContext {
    /// Which lifecycle event triggered this hook.
    pub event: HookEvent,
    /// Current conversation messages.
    pub messages: Vec<Message>,
    /// The tool call being executed (for tool-related events).
    pub tool_call: Option<ToolCall>,
    /// The result of a tool call (for `AfterToolExecution`).
    pub tool_result: Option<ToolResult>,
}

impl HookContext {
    /// Create a minimal context for a given event.
    pub fn new(event: HookEvent, messages: Vec<Message>) -> Self {
        Self {
            event,
            messages,
            tool_call: None,
            tool_result: None,
        }
    }

    /// Attach a tool call to the context.
    pub fn with_tool_call(mut self, tool_call: ToolCall) -> Self {
        self.tool_call = Some(tool_call);
        self
    }

    /// Attach a tool result to the context.
    pub fn with_tool_result(mut self, tool_result: ToolResult) -> Self {
        self.tool_result = Some(tool_result);
        self
    }
}

// ---------------------------------------------------------------------------
// Hook trait
// ---------------------------------------------------------------------------

/// A hook that can observe and influence agent lifecycle events.
#[async_trait]
pub trait Hook: Send + Sync {
    /// Human-readable name for this hook.
    fn name(&self) -> &str;

    /// Which lifecycle events this hook subscribes to.
    fn events(&self) -> &[HookEvent];

    /// Execute the hook for the given event and context.
    async fn execute(
        &self,
        event: &HookEvent,
        context: &HookContext,
    ) -> Result<HookOutcome, String>;
}

// ---------------------------------------------------------------------------
// HookRunner — manages and executes hooks
// ---------------------------------------------------------------------------

/// Manages a collection of hooks and runs them for lifecycle events.
pub struct HookRunner {
    hooks: Vec<Arc<dyn Hook>>,
}

impl HookRunner {
    /// Create an empty hook runner.
    pub fn new() -> Self {
        Self { hooks: Vec::new() }
    }

    /// Register a hook.
    pub fn register(&mut self, hook: Arc<dyn Hook>) {
        self.hooks.push(hook);
    }

    /// Number of registered hooks.
    pub fn len(&self) -> usize {
        self.hooks.len()
    }

    /// Whether any hooks are registered.
    pub fn is_empty(&self) -> bool {
        self.hooks.is_empty()
    }

    /// Run all hooks that subscribe to the given event, in parallel.
    ///
    /// Returns the aggregated outcome: any `Block` takes priority over
    /// `Modify`, which takes priority over `Continue`. If multiple hooks
    /// return `Block`, the first one (by registration order) wins.
    /// If multiple hooks return `Modify`, messages are concatenated.
    pub async fn run(&self, event: &HookEvent, context: &HookContext) -> HookOutcome {
        // Filter to hooks that listen for this event.
        let applicable: Vec<_> = self
            .hooks
            .iter()
            .filter(|h| h.events().contains(event))
            .cloned()
            .collect();

        if applicable.is_empty() {
            return HookOutcome::Continue;
        }

        // Run all applicable hooks in parallel.
        let mut handles = Vec::with_capacity(applicable.len());
        for hook in &applicable {
            let hook = Arc::clone(hook);
            let event = event.clone();
            let context = context.clone();
            handles.push(tokio::spawn(async move {
                (
                    hook.name().to_string(),
                    hook.execute(&event, &context).await,
                )
            }));
        }

        let mut outcomes: Vec<(String, HookOutcome)> = Vec::new();
        for handle in handles {
            match handle.await {
                Ok((name, Ok(outcome))) => outcomes.push((name, outcome)),
                Ok((name, Err(err))) => {
                    tracing::warn!(hook = %name, error = %err, "hook execution failed, treating as Continue");
                }
                Err(err) => {
                    tracing::warn!(error = %err, "hook task panicked, treating as Continue");
                }
            }
        }

        aggregate_outcomes(outcomes)
    }
}

impl Default for HookRunner {
    fn default() -> Self {
        Self::new()
    }
}

/// Aggregate outcomes using priority: Block > Modify > Continue.
///
/// - If any outcome is `Block`, return the first `Block`.
/// - If any outcome is `Modify`, concatenate all `Modify` messages.
/// - Otherwise, return `Continue`.
fn aggregate_outcomes(outcomes: Vec<(String, HookOutcome)>) -> HookOutcome {
    let mut first_block: Option<String> = None;
    let mut modifications: Vec<String> = Vec::new();

    for (_name, outcome) in outcomes {
        match outcome {
            HookOutcome::Block(reason) => {
                if first_block.is_none() {
                    first_block = Some(reason);
                }
            }
            HookOutcome::Modify(msg) => {
                modifications.push(msg);
            }
            HookOutcome::Continue => {}
        }
    }

    if let Some(reason) = first_block {
        HookOutcome::Block(reason)
    } else if !modifications.is_empty() {
        HookOutcome::Modify(modifications.join("\n"))
    } else {
        HookOutcome::Continue
    }
}

// ---------------------------------------------------------------------------
// ShellHook — runs a shell command and maps exit code to outcome
// ---------------------------------------------------------------------------

/// A hook that executes a shell command and interprets the exit code:
/// - Exit 0 → `Continue`
/// - Exit 1 → `Block` (stderr as reason)
/// - Exit 2 → `Modify` (stdout as injected message)
/// - Any other exit code → error (treated as Continue by the runner)
pub struct ShellHook {
    hook_name: String,
    command: String,
    subscribed_events: Vec<HookEvent>,
}

impl ShellHook {
    /// Create a new shell hook.
    ///
    /// `command` is executed via `sh -c` (or `cmd /C` on Windows).
    /// Environment variables are set for the hook:
    /// - `AVA_HOOK_EVENT` — the lifecycle event name
    /// - `AVA_HOOK_TOOL_NAME` — tool name (if applicable)
    /// - `AVA_HOOK_TOOL_ID` — tool call ID (if applicable)
    pub fn new(
        name: impl Into<String>,
        command: impl Into<String>,
        events: Vec<HookEvent>,
    ) -> Self {
        Self {
            hook_name: name.into(),
            command: command.into(),
            subscribed_events: events,
        }
    }
}

#[async_trait]
impl Hook for ShellHook {
    fn name(&self) -> &str {
        &self.hook_name
    }

    fn events(&self) -> &[HookEvent] {
        &self.subscribed_events
    }

    async fn execute(
        &self,
        event: &HookEvent,
        context: &HookContext,
    ) -> Result<HookOutcome, String> {
        let mut cmd = Command::new("sh");
        cmd.arg("-c").arg(&self.command);

        // Set environment variables for the hook process.
        cmd.env("AVA_HOOK_EVENT", event.to_string());
        if let Some(ref tc) = context.tool_call {
            cmd.env("AVA_HOOK_TOOL_NAME", &tc.name);
            cmd.env("AVA_HOOK_TOOL_ID", &tc.id);
        }

        let output = cmd
            .output()
            .await
            .map_err(|e| format!("failed to spawn shell hook: {e}"))?;

        match output.status.code() {
            Some(0) => Ok(HookOutcome::Continue),
            Some(1) => {
                let reason = String::from_utf8_lossy(&output.stderr).trim().to_string();
                Ok(HookOutcome::Block(if reason.is_empty() {
                    "blocked by hook".to_string()
                } else {
                    reason
                }))
            }
            Some(2) => {
                let msg = String::from_utf8_lossy(&output.stdout).trim().to_string();
                Ok(HookOutcome::Modify(if msg.is_empty() {
                    "(empty modification)".to_string()
                } else {
                    msg
                }))
            }
            Some(code) => Err(format!("shell hook exited with unexpected code {code}")),
            None => Err("shell hook terminated by signal".to_string()),
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    /// A simple test hook that always returns a fixed outcome.
    struct FixedHook {
        hook_name: String,
        subscribed: Vec<HookEvent>,
        outcome: HookOutcome,
    }

    impl FixedHook {
        fn new(name: &str, events: Vec<HookEvent>, outcome: HookOutcome) -> Self {
            Self {
                hook_name: name.to_string(),
                subscribed: events,
                outcome,
            }
        }
    }

    #[async_trait]
    impl Hook for FixedHook {
        fn name(&self) -> &str {
            &self.hook_name
        }

        fn events(&self) -> &[HookEvent] {
            &self.subscribed
        }

        async fn execute(
            &self,
            _event: &HookEvent,
            _context: &HookContext,
        ) -> Result<HookOutcome, String> {
            Ok(self.outcome.clone())
        }
    }

    /// A hook that always errors.
    struct ErrorHook;

    #[async_trait]
    impl Hook for ErrorHook {
        fn name(&self) -> &str {
            "error_hook"
        }

        fn events(&self) -> &[HookEvent] {
            &[HookEvent::BeforeModel]
        }

        async fn execute(
            &self,
            _event: &HookEvent,
            _context: &HookContext,
        ) -> Result<HookOutcome, String> {
            Err("simulated failure".to_string())
        }
    }

    fn test_context(event: HookEvent) -> HookContext {
        HookContext::new(event, vec![])
    }

    // -- Registration tests --

    #[test]
    fn hook_runner_starts_empty() {
        let runner = HookRunner::new();
        assert!(runner.is_empty());
        assert_eq!(runner.len(), 0);
    }

    #[test]
    fn hook_registration() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(FixedHook::new(
            "h1",
            vec![HookEvent::SessionStart],
            HookOutcome::Continue,
        )));
        runner.register(Arc::new(FixedHook::new(
            "h2",
            vec![HookEvent::SessionEnd],
            HookOutcome::Continue,
        )));
        assert_eq!(runner.len(), 2);
        assert!(!runner.is_empty());
    }

    // -- Event filtering tests --

    #[tokio::test]
    async fn event_filtering_only_matching_hooks_run() {
        let mut runner = HookRunner::new();
        // This hook only listens to SessionStart
        runner.register(Arc::new(FixedHook::new(
            "start_only",
            vec![HookEvent::SessionStart],
            HookOutcome::Block("blocked".into()),
        )));

        // Firing BeforeModel should not trigger the SessionStart hook
        let ctx = test_context(HookEvent::BeforeModel);
        let outcome = runner.run(&HookEvent::BeforeModel, &ctx).await;
        assert_eq!(outcome, HookOutcome::Continue);

        // Firing SessionStart should trigger the hook
        let ctx = test_context(HookEvent::SessionStart);
        let outcome = runner.run(&HookEvent::SessionStart, &ctx).await;
        assert_eq!(outcome, HookOutcome::Block("blocked".into()));
    }

    #[tokio::test]
    async fn hook_subscribes_to_multiple_events() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(FixedHook::new(
            "multi",
            vec![HookEvent::BeforeModel, HookEvent::AfterModel],
            HookOutcome::Modify("injected".into()),
        )));

        let ctx = test_context(HookEvent::BeforeModel);
        let outcome = runner.run(&HookEvent::BeforeModel, &ctx).await;
        assert_eq!(outcome, HookOutcome::Modify("injected".into()));

        let ctx = test_context(HookEvent::AfterModel);
        let outcome = runner.run(&HookEvent::AfterModel, &ctx).await;
        assert_eq!(outcome, HookOutcome::Modify("injected".into()));
    }

    // -- Outcome aggregation tests --

    #[tokio::test]
    async fn all_continue_yields_continue() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(FixedHook::new(
            "c1",
            vec![HookEvent::SessionStart],
            HookOutcome::Continue,
        )));
        runner.register(Arc::new(FixedHook::new(
            "c2",
            vec![HookEvent::SessionStart],
            HookOutcome::Continue,
        )));

        let ctx = test_context(HookEvent::SessionStart);
        let outcome = runner.run(&HookEvent::SessionStart, &ctx).await;
        assert_eq!(outcome, HookOutcome::Continue);
    }

    #[tokio::test]
    async fn block_takes_priority_over_modify_and_continue() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(FixedHook::new(
            "continue_hook",
            vec![HookEvent::BeforeToolExecution],
            HookOutcome::Continue,
        )));
        runner.register(Arc::new(FixedHook::new(
            "modify_hook",
            vec![HookEvent::BeforeToolExecution],
            HookOutcome::Modify("extra context".into()),
        )));
        runner.register(Arc::new(FixedHook::new(
            "block_hook",
            vec![HookEvent::BeforeToolExecution],
            HookOutcome::Block("not allowed".into()),
        )));

        let ctx = test_context(HookEvent::BeforeToolExecution);
        let outcome = runner.run(&HookEvent::BeforeToolExecution, &ctx).await;
        assert_eq!(outcome, HookOutcome::Block("not allowed".into()));
    }

    #[tokio::test]
    async fn modify_takes_priority_over_continue() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(FixedHook::new(
            "c",
            vec![HookEvent::AfterModel],
            HookOutcome::Continue,
        )));
        runner.register(Arc::new(FixedHook::new(
            "m",
            vec![HookEvent::AfterModel],
            HookOutcome::Modify("injected".into()),
        )));

        let ctx = test_context(HookEvent::AfterModel);
        let outcome = runner.run(&HookEvent::AfterModel, &ctx).await;
        assert_eq!(outcome, HookOutcome::Modify("injected".into()));
    }

    #[tokio::test]
    async fn multiple_modify_messages_concatenated() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(FixedHook::new(
            "m1",
            vec![HookEvent::BeforeModel],
            HookOutcome::Modify("first".into()),
        )));
        runner.register(Arc::new(FixedHook::new(
            "m2",
            vec![HookEvent::BeforeModel],
            HookOutcome::Modify("second".into()),
        )));

        let ctx = test_context(HookEvent::BeforeModel);
        let outcome = runner.run(&HookEvent::BeforeModel, &ctx).await;
        // Both modify messages should be present (order may vary due to parallel execution)
        match outcome {
            HookOutcome::Modify(msg) => {
                assert!(msg.contains("first"));
                assert!(msg.contains("second"));
            }
            other => panic!("expected Modify, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn erroring_hook_treated_as_continue() {
        let mut runner = HookRunner::new();
        runner.register(Arc::new(ErrorHook));

        let ctx = test_context(HookEvent::BeforeModel);
        let outcome = runner.run(&HookEvent::BeforeModel, &ctx).await;
        assert_eq!(outcome, HookOutcome::Continue);
    }

    #[tokio::test]
    async fn no_hooks_for_event_yields_continue() {
        let runner = HookRunner::new();
        let ctx = test_context(HookEvent::SessionEnd);
        let outcome = runner.run(&HookEvent::SessionEnd, &ctx).await;
        assert_eq!(outcome, HookOutcome::Continue);
    }

    // -- HookContext builder tests --

    #[test]
    fn hook_context_with_tool_call() {
        let tc = ToolCall {
            id: "tc_1".into(),
            name: "bash".into(),
            arguments: serde_json::json!({"command": "ls"}),
        };
        let ctx = HookContext::new(HookEvent::BeforeToolExecution, vec![]).with_tool_call(tc);
        assert!(ctx.tool_call.is_some());
        assert_eq!(ctx.tool_call.as_ref().unwrap().name, "bash");
    }

    #[test]
    fn hook_context_with_tool_result() {
        let tr = ToolResult {
            call_id: "tc_1".into(),
            content: "file.txt".into(),
            is_error: false,
        };
        let ctx = HookContext::new(HookEvent::AfterToolExecution, vec![]).with_tool_result(tr);
        assert!(ctx.tool_result.is_some());
        assert!(!ctx.tool_result.as_ref().unwrap().is_error);
    }

    // -- HookOutcome priority unit test --

    #[test]
    fn outcome_priority_ordering() {
        assert!(
            HookOutcome::Block("x".into()).priority() > HookOutcome::Modify("y".into()).priority()
        );
        assert!(HookOutcome::Modify("y".into()).priority() > HookOutcome::Continue.priority());
    }

    // -- HookEvent display --

    #[test]
    fn hook_event_display() {
        assert_eq!(HookEvent::SessionStart.to_string(), "session_start");
        assert_eq!(HookEvent::BeforeModel.to_string(), "before_model");
        assert_eq!(HookEvent::AfterModel.to_string(), "after_model");
        assert_eq!(
            HookEvent::BeforeToolExecution.to_string(),
            "before_tool_execution"
        );
        assert_eq!(
            HookEvent::AfterToolExecution.to_string(),
            "after_tool_execution"
        );
        assert_eq!(HookEvent::SessionEnd.to_string(), "session_end");
    }

    // -- ShellHook tests --

    #[tokio::test]
    async fn shell_hook_exit_0_continue() {
        let hook = ShellHook::new("test", "exit 0", vec![HookEvent::SessionStart]);
        let ctx = test_context(HookEvent::SessionStart);
        let outcome = hook.execute(&HookEvent::SessionStart, &ctx).await.unwrap();
        assert_eq!(outcome, HookOutcome::Continue);
    }

    #[tokio::test]
    async fn shell_hook_exit_1_block() {
        let hook = ShellHook::new(
            "blocker",
            "echo 'denied' >&2; exit 1",
            vec![HookEvent::BeforeToolExecution],
        );
        let ctx = test_context(HookEvent::BeforeToolExecution);
        let outcome = hook
            .execute(&HookEvent::BeforeToolExecution, &ctx)
            .await
            .unwrap();
        assert_eq!(outcome, HookOutcome::Block("denied".into()));
    }

    #[tokio::test]
    async fn shell_hook_exit_1_empty_stderr_uses_default() {
        let hook = ShellHook::new("blocker", "exit 1", vec![HookEvent::BeforeToolExecution]);
        let ctx = test_context(HookEvent::BeforeToolExecution);
        let outcome = hook
            .execute(&HookEvent::BeforeToolExecution, &ctx)
            .await
            .unwrap();
        assert_eq!(outcome, HookOutcome::Block("blocked by hook".into()));
    }

    #[tokio::test]
    async fn shell_hook_exit_2_modify() {
        // exit 2 maps to Modify with stdout as the message
        let hook = ShellHook::new(
            "modifier",
            "echo 'extra context'; exit 2",
            vec![HookEvent::BeforeModel],
        );
        let ctx = test_context(HookEvent::BeforeModel);
        let outcome = hook.execute(&HookEvent::BeforeModel, &ctx).await.unwrap();
        assert_eq!(outcome, HookOutcome::Modify("extra context".into()));
    }

    #[tokio::test]
    async fn shell_hook_unexpected_exit_code_is_error() {
        let hook = ShellHook::new("bad", "exit 42", vec![HookEvent::SessionStart]);
        let ctx = test_context(HookEvent::SessionStart);
        let result = hook.execute(&HookEvent::SessionStart, &ctx).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("42"));
    }

    #[tokio::test]
    async fn shell_hook_receives_env_vars() {
        let hook = ShellHook::new(
            "env_check",
            r#"test "$AVA_HOOK_EVENT" = "before_tool_execution" && test "$AVA_HOOK_TOOL_NAME" = "bash""#,
            vec![HookEvent::BeforeToolExecution],
        );
        let tc = ToolCall {
            id: "tc_1".into(),
            name: "bash".into(),
            arguments: serde_json::json!({}),
        };
        let ctx = HookContext::new(HookEvent::BeforeToolExecution, vec![]).with_tool_call(tc);
        let outcome = hook
            .execute(&HookEvent::BeforeToolExecution, &ctx)
            .await
            .unwrap();
        assert_eq!(outcome, HookOutcome::Continue);
    }
}
