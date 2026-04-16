use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};
use tokio::sync::{mpsc, oneshot};

use crate::registry::Tool;

/// A request from the agent to ask the user a question.
#[derive(Debug)]
pub struct QuestionRequest {
    /// Optional run correlation for the originating agent run.
    pub run_id: Option<String>,
    /// The question text to display.
    pub question: String,
    /// Optional selectable choices. If empty, show a free-text input.
    pub options: Vec<String>,
    /// Channel to send the user's answer back to the tool.
    pub reply: oneshot::Sender<String>,
}

/// Bridge between the question tool (agent side) and the TUI (UI side).
///
/// The tool sends a `QuestionRequest` through this bridge; the TUI receives it,
/// shows a modal/prompt, collects the user's answer, and sends it back via the
/// oneshot channel embedded in the request.
#[derive(Clone)]
pub struct QuestionBridge {
    tx: mpsc::UnboundedSender<QuestionRequest>,
    run_id: Option<String>,
}

impl QuestionBridge {
    /// Create a new bridge, returning the bridge handle and the receiving end.
    pub fn new() -> (Self, mpsc::UnboundedReceiver<QuestionRequest>) {
        let (tx, rx) = mpsc::unbounded_channel();
        (Self { tx, run_id: None }, rx)
    }

    pub fn with_run_id(&self, run_id: Option<String>) -> Self {
        Self {
            tx: self.tx.clone(),
            run_id,
        }
    }
}

/// Tool that asks the user a question and waits for their response.
///
/// When the agent needs clarification or user input, it calls this tool.
/// The question is routed to the TUI, which displays a modal and collects
/// the answer. The answer is returned as the tool result.
pub struct QuestionTool {
    bridge: QuestionBridge,
}

impl QuestionTool {
    pub fn new(bridge: QuestionBridge) -> Self {
        Self { bridge }
    }
}

#[async_trait]
impl Tool for QuestionTool {
    fn name(&self) -> &str {
        "question"
    }

    fn description(&self) -> &str {
        "Ask the user a question and wait for their answer. Use this when you need \
         clarification, confirmation, or user input to proceed. You can optionally \
         provide a list of choices for the user to select from."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["question"],
            "properties": {
                "question": {
                    "type": "string",
                    "description": "The question to ask the user"
                },
                "options": {
                    "type": "array",
                    "items": { "type": "string" },
                    "description": "Optional list of choices. If provided, the user selects from these instead of typing a free-text answer."
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let question = args
            .get("question")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: question".into()))?
            .to_string();

        let options: Vec<String> = args
            .get("options")
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(Value::as_str)
                    .map(String::from)
                    .collect()
            })
            .unwrap_or_default();

        tracing::debug!(tool = "question", %question, options_count = options.len(), "executing question tool");

        let (reply_tx, reply_rx) = oneshot::channel();

        self.bridge
            .tx
            .send(QuestionRequest {
                run_id: self.bridge.run_id.clone(),
                question: question.clone(),
                options,
                reply: reply_tx,
            })
            .map_err(|_| {
                AvaError::ToolError(
                    "Failed to send question to UI — the TUI may not be running".to_string(),
                )
            })?;

        // Wait for the user's answer with a 5-minute timeout
        let answer = tokio::time::timeout(std::time::Duration::from_secs(300), reply_rx)
            .await
            .map_err(|_| {
                AvaError::TimeoutError("User did not respond within 5 minutes".to_string())
            })?
            .map_err(|_| {
                AvaError::ToolError(
                    "Question was not answered — the UI channel was closed".to_string(),
                )
            })?;

        if answer.is_empty() {
            Ok(ToolResult {
                call_id: String::new(),
                content: "The user declined to answer the question.".to_string(),
                is_error: false,
            })
        } else {
            Ok(ToolResult {
                call_id: String::new(),
                content: format!("User's answer: {answer}"),
                is_error: false,
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn question_tool_metadata() {
        let (bridge, _rx) = QuestionBridge::new();
        let tool = QuestionTool::new(bridge);
        assert_eq!(tool.name(), "question");
        assert!(!tool.description().is_empty());
        let params = tool.parameters();
        assert_eq!(params["required"], json!(["question"]));
    }

    #[tokio::test]
    async fn question_tool_receives_answer() {
        let (bridge, mut rx) = QuestionBridge::new();
        let tool = QuestionTool::new(bridge);

        // Simulate the TUI answering in a separate task
        let handle = tokio::spawn(async move {
            let req = rx.recv().await.unwrap();
            assert_eq!(req.question, "What language?");
            assert!(req.options.is_empty());
            req.reply.send("Rust".to_string()).unwrap();
        });

        let result = tool
            .execute(json!({"question": "What language?"}))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Rust"));

        handle.await.unwrap();
    }

    #[tokio::test]
    async fn question_tool_with_options() {
        let (bridge, mut rx) = QuestionBridge::new();
        let tool = QuestionTool::new(bridge);

        let handle = tokio::spawn(async move {
            let req = rx.recv().await.unwrap();
            assert_eq!(req.options, vec!["Yes", "No", "Maybe"]);
            req.reply.send("Yes".to_string()).unwrap();
        });

        let result = tool
            .execute(json!({
                "question": "Continue?",
                "options": ["Yes", "No", "Maybe"]
            }))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Yes"));

        handle.await.unwrap();
    }

    #[tokio::test]
    async fn question_tool_empty_answer() {
        let (bridge, mut rx) = QuestionBridge::new();
        let tool = QuestionTool::new(bridge);

        let handle = tokio::spawn(async move {
            let req = rx.recv().await.unwrap();
            req.reply.send(String::new()).unwrap();
        });

        let result = tool.execute(json!({"question": "What?"})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("declined"));

        handle.await.unwrap();
    }

    #[tokio::test]
    async fn question_tool_missing_question_errors() {
        let (bridge, _rx) = QuestionBridge::new();
        let tool = QuestionTool::new(bridge);
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }
}
