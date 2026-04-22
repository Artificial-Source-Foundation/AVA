use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_agent::agent_loop::AgentEvent;
use ava_agent_orchestration::stack::{AgentStack, AgentStackConfig};
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_permissions::tags::RiskLevel;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::{AvaError, Message, Result, StreamChunk};
use futures::Stream;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

fn completion_response(result: &str) -> String {
    format!(
        r#"{{"tool_calls":[{{"name":"attempt_completion","arguments":{{"result":"{result}"}}}}]}}"#
    )
}

fn spawn_recording_unattended_approval_resolver(
    mut approval_rx: mpsc::UnboundedReceiver<ApprovalRequest>,
    seen_risks: Arc<tokio::sync::Mutex<Vec<RiskLevel>>>,
) {
    tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            seen_risks.lock().await.push(req.inspection.risk_level);
            let decision = match req.inspection.risk_level {
                RiskLevel::Safe | RiskLevel::Low | RiskLevel::Medium => ToolApproval::Allowed,
                RiskLevel::High | RiskLevel::Critical => ToolApproval::Rejected(Some(format!(
                    "Headless mode rejected dangerous action '{}': {} (risk: {:?})",
                    req.call.name, req.inspection.reason, req.inspection.risk_level
                ))),
            };
            let _ = req.reply.send(decision);
        }
    });
}

#[tokio::test]
#[ignore = "requires mock provider to honour tool-call write paths; tracked for fix"]
async fn full_agent_run_with_tool_calls() {
    let dir = tempfile::tempdir().expect("tempdir");
    let test_file = dir.path().join("hello.txt");
    let output_file = dir.path().join("output.txt");
    std::fs::write(&test_file, "Hello World").expect("seed file");

    let responses = vec![
        format!(
            r#"{{"tool_calls":[{{"name":"read","arguments":{{"path":"{}"}}}}]}}"#,
            test_file.to_string_lossy()
        ),
        format!(
            r#"{{"tool_calls":[{{"name":"write","arguments":{{"path":"{}","content":"Hello from AVA!"}}}}]}}"#,
            output_file.to_string_lossy()
        ),
        completion_response("Done"),
    ];

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        auto_approve: true,
        injected_provider: Some(Arc::new(MockProvider::new("test-model", responses))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "Read hello.txt and write output",
            10,
            Some(tx),
            cancel,
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
    assert!(result.turns >= 3);
    assert!(output_file.exists());
    assert_eq!(
        std::fs::read_to_string(&output_file).expect("read output"),
        "Hello from AVA!"
    );

    let mut events: Vec<AgentEvent> = Vec::new();
    while let Ok(event) = rx.try_recv() {
        events.push(event);
    }
    assert!(!events.is_empty());
}

#[tokio::test]
async fn agent_run_with_bash_tool() {
    let dir = tempfile::tempdir().expect("tempdir");
    let responses = vec![
        r#"{"tool_calls":[{"name":"bash","arguments":{"command":"echo hello"}}]}"#.to_string(),
        completion_response("bash done"),
    ];

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        auto_approve: true,
        injected_provider: Some(Arc::new(MockProvider::new("test-model", responses))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let result = stack
        .run(
            "run bash",
            10,
            None,
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect("run should succeed");
    assert!(result.success);
    assert!(result
        .session
        .messages
        .iter()
        .any(|msg| msg.content.contains("hello")));
}

#[tokio::test]
async fn non_interactive_runtime_keeps_safe_bash_unattended() {
    let dir = tempfile::tempdir().expect("tempdir");
    let responses = vec![
        r#"{"tool_calls":[{"name":"bash","arguments":{"command":"printf safe-headless"}}]}"#
            .to_string(),
        completion_response("safe bash done"),
    ];

    let seen_risks = Arc::new(tokio::sync::Mutex::new(Vec::new()));
    let (stack, _question_rx, approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        auto_approve: true,
        non_interactive_approvals: true,
        injected_provider: Some(Arc::new(MockProvider::new("test-model", responses))),
        working_dir: Some(dir.path().to_path_buf()),
        max_turns: 4,
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");
    spawn_recording_unattended_approval_resolver(approval_rx, seen_risks.clone());

    let (tx, mut rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "run safe bash",
            4,
            Some(tx),
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
    assert!(
        result
            .session
            .messages
            .iter()
            .any(|msg| msg.content.contains("safe-headless")),
        "safe bash output should be preserved in the session"
    );

    let mut events = Vec::new();
    while let Ok(event) = rx.try_recv() {
        events.push(event);
    }
    assert!(events.iter().any(
        |event| matches!(event, AgentEvent::ToolResult(result) if !result.is_error && result.content.contains("safe-headless"))
    ));
    assert!(
        seen_risks.lock().await.is_empty(),
        "safe bash should stay on the normal allow path without approval bridging"
    );
}

#[tokio::test]
async fn non_interactive_runtime_rejects_dangerous_bash_even_with_auto_approve() {
    let dir = tempfile::tempdir().expect("tempdir");
    let responses = vec![
        r#"{"tool_calls":[{"name":"bash","arguments":{"command":"rm -rf /tmp/ava-headless-dangerous-path"}}]}"#
            .to_string(),
        completion_response("dangerous bash handled"),
    ];

    let seen_risks = Arc::new(tokio::sync::Mutex::new(Vec::new()));
    let (stack, _question_rx, approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        auto_approve: true,
        non_interactive_approvals: true,
        injected_provider: Some(Arc::new(MockProvider::new("test-model", responses))),
        working_dir: Some(dir.path().to_path_buf()),
        max_turns: 4,
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");
    spawn_recording_unattended_approval_resolver(approval_rx, seen_risks.clone());

    let (tx, mut rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "run dangerous bash",
            4,
            Some(tx),
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect("run should succeed after the rejected tool result");

    assert!(result.success);

    let mut events = Vec::new();
    while let Ok(event) = rx.try_recv() {
        events.push(event);
    }

    assert!(events
        .iter()
        .any(|event| matches!(event, AgentEvent::ToolCall(call) if call.name == "bash")));
    assert!(events
        .iter()
        .any(|event| matches!(event, AgentEvent::ToolResult(result)
            if result.is_error
                && result.content.contains("Headless mode rejected dangerous action 'bash'")
                && result.content.contains("risk: High"))));

    let seen_risks = seen_risks.lock().await;
    assert_eq!(seen_risks.as_slice(), &[RiskLevel::High]);
}

#[tokio::test]
async fn agent_run_cancellation() {
    let dir = tempfile::tempdir().expect("tempdir");
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(Arc::new(SlowProvider {
            model: "slow".to_string(),
            delay: Duration::from_millis(250),
        })),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_millis(10)).await;
        cancel_clone.cancel();
    });

    let err = stack
        .run(
            "slow run",
            5,
            None,
            cancel,
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect_err("run should be cancelled");
    assert!(matches!(err, AvaError::Cancelled));
}

struct SlowProvider {
    model: String,
    delay: Duration,
}

#[async_trait]
impl LLMProvider for SlowProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        tokio::time::sleep(self.delay).await;
        Ok(completion_response("slow"))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let out = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![StreamChunk::text(
            out,
        )])))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}
