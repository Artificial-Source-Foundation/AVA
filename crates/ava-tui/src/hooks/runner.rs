use super::config::{HookAction, HookRegistry};
use super::events::{HookContext, HookEvent};
use std::time::Duration;
use tracing::{debug, info, warn};

/// The outcome of running hooks for an event.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum HookResult {
    /// All hooks passed (or no hooks matched). Proceed normally.
    Allow,
    /// A hook requested blocking the action. Contains the reason.
    Block(String),
    /// A hook encountered an error. Contains the error message.
    /// Errors are non-fatal — logged but do not block the action.
    Error(String),
}

impl HookResult {
    pub fn is_blocked(&self) -> bool {
        matches!(self, Self::Block(_))
    }
}

/// Detailed result of a single hook execution.
#[derive(Debug, Clone)]
pub struct HookExecution {
    /// Description or identifier of the hook.
    pub description: String,
    /// The event that triggered it.
    pub event: HookEvent,
    /// The outcome.
    pub result: HookResult,
    /// How long the hook took to execute.
    pub duration: Duration,
}

/// Executes hooks for lifecycle events.
pub struct HookRunner;

impl HookRunner {
    /// Run all matching hooks for the given event and context.
    ///
    /// For `PreToolUse` events, a hook returning exit code 2 (command) will
    /// result in `HookResult::Block`, preventing the tool from executing.
    ///
    /// Hooks run sequentially in priority order. If any hook blocks, remaining
    /// hooks are skipped.
    pub async fn run_hooks(
        registry: &HookRegistry,
        event: HookEvent,
        context: HookContext,
    ) -> (HookResult, Vec<HookExecution>) {
        let hooks = registry.hooks_for_event(&event);
        let mut executions = Vec::new();

        if hooks.is_empty() {
            return (HookResult::Allow, executions);
        }

        let context_json = serde_json::to_string(&context).unwrap_or_default();

        for hook in hooks {
            // Check if this hook matches the context
            if !HookRegistry::matches(hook, &context) {
                continue;
            }

            let description = hook
                .description
                .clone()
                .unwrap_or_else(|| format!("{} hook (pri {})", hook.event, hook.priority));

            let start = std::time::Instant::now();

            let result = match &hook.action {
                HookAction::Command {
                    command,
                    timeout,
                    cwd,
                } => Self::run_command(command, *timeout, cwd.as_deref(), &context_json).await,
                HookAction::Http {
                    url,
                    headers,
                    timeout,
                } => Self::run_http(url, headers, *timeout, &context_json).await,
                HookAction::Prompt { prompt } => {
                    // Prompt hooks are a stub — they require LLM access which
                    // would create a circular dependency. Log and allow.
                    info!(prompt = %prompt, "prompt hook (stub — allowing)");
                    HookResult::Allow
                }
            };

            let duration = start.elapsed();

            debug!(
                hook = %description,
                event = %event,
                result = ?result,
                duration_ms = duration.as_millis(),
                "hook executed"
            );

            let execution = HookExecution {
                description: description.clone(),
                event: event.clone(),
                result: result.clone(),
                duration,
            };
            executions.push(execution);

            // If a hook blocks, stop processing remaining hooks
            if result.is_blocked() {
                info!(hook = %description, "hook blocked action");
                return (result, executions);
            }
        }

        // If any hook errored, report the first error (non-fatal)
        let overall = executions
            .iter()
            .find(|e| matches!(e.result, HookResult::Error(_)))
            .map(|e| e.result.clone())
            .unwrap_or(HookResult::Allow);

        (overall, executions)
    }

    /// Run all matching hooks and return just the result (convenience wrapper).
    pub async fn fire(
        registry: &HookRegistry,
        event: HookEvent,
        context: HookContext,
    ) -> HookResult {
        let (result, _) = Self::run_hooks(registry, event, context).await;
        result
    }

    /// Execute a shell command hook.
    async fn run_command(
        command: &str,
        timeout_secs: u64,
        cwd: Option<&str>,
        context_json: &str,
    ) -> HookResult {
        let mut cmd = tokio::process::Command::new("sh");
        cmd.arg("-c").arg(command);
        cmd.stdin(std::process::Stdio::piped());
        cmd.stdout(std::process::Stdio::piped());
        cmd.stderr(std::process::Stdio::piped());
        cmd.kill_on_drop(true);

        if let Some(dir) = cwd {
            cmd.current_dir(dir);
        }

        let child = match cmd.spawn() {
            Ok(child) => child,
            Err(e) => {
                warn!(command = %command, error = %e, "failed to spawn hook command");
                return HookResult::Error(format!("Failed to spawn: {e}"));
            }
        };

        // Write context JSON to stdin
        let mut child = child;
        if let Some(mut stdin) = child.stdin.take() {
            use tokio::io::AsyncWriteExt;
            let json = context_json.to_string();
            tokio::spawn(async move {
                let _ = stdin.write_all(json.as_bytes()).await;
                let _ = stdin.shutdown().await;
            });
        }

        // Wait with timeout. kill_on_drop(true) is set as a safety net,
        // but on timeout we also kill explicitly so the process is reaped
        // immediately rather than waiting for the Child to be dropped.
        let timeout = Duration::from_secs(timeout_secs);
        match tokio::time::timeout(timeout, child.wait_with_output()).await {
            Ok(Ok(output)) => {
                let exit_code = output.status.code().unwrap_or(-1);
                let stderr = String::from_utf8_lossy(&output.stderr);

                match exit_code {
                    0 => {
                        debug!(command = %command, "hook command succeeded");
                        HookResult::Allow
                    }
                    2 => {
                        // Exit code 2 = block the action
                        let reason = if stderr.is_empty() {
                            "Hook blocked the action".to_string()
                        } else {
                            stderr.trim().to_string()
                        };
                        HookResult::Block(reason)
                    }
                    code => {
                        let msg = if stderr.is_empty() {
                            format!("Hook exited with code {code}")
                        } else {
                            format!("Hook exited with code {code}: {}", stderr.trim())
                        };
                        warn!(command = %command, code, "hook command failed");
                        HookResult::Error(msg)
                    }
                }
            }
            Ok(Err(e)) => {
                warn!(command = %command, error = %e, "hook command IO error");
                HookResult::Error(format!("IO error: {e}"))
            }
            Err(_) => {
                // Timeout fired. wait_with_output() consumed the Child, so
                // when the timeout cancels the future the Child is dropped
                // and kill_on_drop(true) sends SIGKILL + reaps it.
                warn!(command = %command, timeout_secs, "hook command timed out");
                HookResult::Error(format!("Hook timed out after {timeout_secs}s"))
            }
        }
    }

    /// Execute an HTTP webhook hook.
    async fn run_http(
        url: &str,
        headers: &std::collections::HashMap<String, String>,
        timeout_secs: u64,
        context_json: &str,
    ) -> HookResult {
        // Build a one-shot HTTP client
        let client = match reqwest::Client::builder()
            .timeout(Duration::from_secs(timeout_secs))
            .build()
        {
            Ok(c) => c,
            Err(e) => return HookResult::Error(format!("Failed to build HTTP client: {e}")),
        };

        let mut request = client
            .post(url)
            .header("Content-Type", "application/json")
            .body(context_json.to_string());

        for (key, value) in headers {
            request = request.header(key.as_str(), value.as_str());
        }

        match request.send().await {
            Ok(response) => {
                let status = response.status();
                if status.is_success() {
                    debug!(url = %url, status = %status, "webhook succeeded");
                    HookResult::Allow
                } else {
                    let body = response.text().await.unwrap_or_else(|_| String::new());
                    let msg = format!("Webhook returned {status}: {body}");
                    warn!(url = %url, status = %status, "webhook failed");
                    HookResult::Error(msg)
                }
            }
            Err(e) => {
                warn!(url = %url, error = %e, "webhook request failed");
                HookResult::Error(format!("Webhook error: {e}"))
            }
        }
    }

    /// Simulate running hooks for dry-run mode. Returns which hooks would
    /// fire without actually executing them.
    pub fn dry_run(
        registry: &HookRegistry,
        event: &HookEvent,
        context: &HookContext,
    ) -> Vec<String> {
        let hooks = registry.hooks_for_event(event);
        hooks
            .into_iter()
            .filter(|h| HookRegistry::matches(h, context))
            .map(|h| {
                let desc = h
                    .description
                    .clone()
                    .unwrap_or_else(|| format!("{} hook", h.event));
                let action_type = match &h.action {
                    HookAction::Command { command, .. } => {
                        format!("command: {}", truncate(command, 60))
                    }
                    HookAction::Http { url, .. } => format!("http: {url}"),
                    HookAction::Prompt { prompt } => {
                        format!("prompt: {}", truncate(prompt, 60))
                    }
                };
                format!(
                    "  [pri {}] {} — {} ({})",
                    h.priority,
                    desc,
                    action_type,
                    h.source.label()
                )
            })
            .collect()
    }
}

fn truncate(s: &str, max: usize) -> String {
    crate::text_utils::truncate_display(s, max)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::hooks::config::{HookAction, HookConfig, HookSource};

    fn make_registry(hooks: Vec<HookConfig>) -> HookRegistry {
        HookRegistry { hooks }
    }

    fn echo_hook(event: &str, command: &str, priority: i32) -> HookConfig {
        HookConfig {
            event: event.to_string(),
            description: Some(format!("test: {command}")),
            matcher: None,
            path_pattern: None,
            priority,
            enabled: true,
            action: HookAction::Command {
                command: command.to_string(),
                timeout: 5,
                cwd: None,
            },
            source: HookSource::Project,
        }
    }

    #[tokio::test]
    async fn no_hooks_returns_allow() {
        let registry = make_registry(vec![]);
        let ctx = HookContext::for_event(&HookEvent::Stop);
        let (result, execs) = HookRunner::run_hooks(&registry, HookEvent::Stop, ctx).await;
        assert_eq!(result, HookResult::Allow);
        assert!(execs.is_empty());
    }

    #[tokio::test]
    async fn command_hook_exit_0_allows() {
        let registry = make_registry(vec![echo_hook("Stop", "exit 0", 100)]);
        let ctx = HookContext::for_event(&HookEvent::Stop);
        let (result, execs) = HookRunner::run_hooks(&registry, HookEvent::Stop, ctx).await;
        assert_eq!(result, HookResult::Allow);
        assert_eq!(execs.len(), 1);
    }

    #[tokio::test]
    async fn command_hook_exit_2_blocks() {
        let registry = make_registry(vec![echo_hook(
            "PreToolUse",
            "echo 'blocked' >&2; exit 2",
            100,
        )]);
        let ctx = HookContext::for_event(&HookEvent::PreToolUse);
        // No matcher, so it matches all tool calls
        let (result, execs) = HookRunner::run_hooks(&registry, HookEvent::PreToolUse, ctx).await;
        assert!(result.is_blocked());
        assert_eq!(execs.len(), 1);
        if let HookResult::Block(reason) = &result {
            assert!(reason.contains("blocked"));
        }
    }

    #[tokio::test]
    async fn command_hook_exit_1_errors() {
        let registry = make_registry(vec![echo_hook("Stop", "echo 'oops' >&2; exit 1", 100)]);
        let ctx = HookContext::for_event(&HookEvent::Stop);
        let (result, _execs) = HookRunner::run_hooks(&registry, HookEvent::Stop, ctx).await;
        assert!(matches!(result, HookResult::Error(_)));
    }

    #[tokio::test]
    async fn command_hook_receives_context_on_stdin() {
        // Command reads stdin and checks for expected content
        let registry = make_registry(vec![echo_hook(
            "PreToolUse",
            r#"input=$(cat); echo "$input" | grep -q '"event":"PreToolUse"' && exit 0 || exit 1"#,
            100,
        )]);
        let ctx = HookContext::for_event(&HookEvent::PreToolUse);
        let (result, _) = HookRunner::run_hooks(&registry, HookEvent::PreToolUse, ctx).await;
        assert_eq!(result, HookResult::Allow);
    }

    #[tokio::test]
    async fn command_hook_timeout() {
        let hook = HookConfig {
            event: "Stop".to_string(),
            description: Some("slow hook".to_string()),
            matcher: None,
            path_pattern: None,
            priority: 100,
            enabled: true,
            action: HookAction::Command {
                command: "sleep 30".to_string(),
                timeout: 1, // 1 second timeout
                cwd: None,
            },
            source: HookSource::Project,
        };
        let registry = make_registry(vec![hook]);
        let ctx = HookContext::for_event(&HookEvent::Stop);
        let (result, _) = HookRunner::run_hooks(&registry, HookEvent::Stop, ctx).await;
        assert!(matches!(result, HookResult::Error(_)));
        if let HookResult::Error(msg) = &result {
            assert!(msg.contains("timed out"));
        }
    }

    #[tokio::test]
    async fn block_stops_remaining_hooks() {
        let registry = make_registry(vec![
            echo_hook("PreToolUse", "exit 2", 10),  // blocks
            echo_hook("PreToolUse", "exit 0", 100), // should not run
        ]);
        let ctx = HookContext::for_event(&HookEvent::PreToolUse);
        let (result, execs) = HookRunner::run_hooks(&registry, HookEvent::PreToolUse, ctx).await;
        assert!(result.is_blocked());
        assert_eq!(execs.len(), 1); // only first hook ran
    }

    #[tokio::test]
    async fn matcher_filters_hooks() {
        let mut hook = echo_hook("PostToolUse", "exit 0", 100);
        hook.matcher = Some("edit|write".to_string());

        let registry = make_registry(vec![hook]);

        // Matching tool name
        let mut ctx = HookContext::for_event(&HookEvent::PostToolUse);
        ctx.tool_name = Some("edit".to_string());
        let (result, execs) = HookRunner::run_hooks(&registry, HookEvent::PostToolUse, ctx).await;
        assert_eq!(result, HookResult::Allow);
        assert_eq!(execs.len(), 1);

        // Non-matching tool name
        let mut ctx = HookContext::for_event(&HookEvent::PostToolUse);
        ctx.tool_name = Some("bash".to_string());
        let (result, execs) = HookRunner::run_hooks(&registry, HookEvent::PostToolUse, ctx).await;
        assert_eq!(result, HookResult::Allow);
        assert_eq!(execs.len(), 0); // filtered out
    }

    #[test]
    fn dry_run_lists_matching_hooks() {
        let mut hook = echo_hook("PostToolUse", "cargo fmt", 50);
        hook.matcher = Some("edit".to_string());
        hook.description = Some("Auto-format".to_string());

        let registry = make_registry(vec![hook]);

        let mut ctx = HookContext::for_event(&HookEvent::PostToolUse);
        ctx.tool_name = Some("edit".to_string());

        let lines = HookRunner::dry_run(&registry, &HookEvent::PostToolUse, &ctx);
        assert_eq!(lines.len(), 1);
        assert!(lines[0].contains("Auto-format"));
        assert!(lines[0].contains("cargo fmt"));
        assert!(lines[0].contains("pri 50"));
    }
}
