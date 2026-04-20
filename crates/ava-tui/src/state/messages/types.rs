use std::time::Duration;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MessageKind {
    User,
    Assistant,
    ToolCall,
    ToolResult,
    Thinking,
    Error,
    System,
    SubAgent,
}

/// Extra data for sub-agent (task tool) messages.
#[derive(Debug, Clone)]
pub struct SubAgentData {
    /// Specialist/subagent type label when known.
    pub agent_type: Option<String>,
    /// The task prompt/description sent to the sub-agent.
    pub description: String,
    /// Whether the sub-agent was launched in background mode.
    pub background: bool,
    /// Number of tools the sub-agent used (populated on completion).
    pub tool_count: usize,
    /// Current tool name while the sub-agent is running.
    pub current_tool: Option<String>,
    /// How long the sub-agent took (populated on completion).
    pub duration: Option<Duration>,
    /// Whether the sub-agent is still executing.
    pub is_running: bool,
    /// Whether the sub-agent failed (set on completion from `ToolResult.is_error`).
    pub failed: bool,
    /// The tool call ID, used to match the ToolResult back.
    pub call_id: String,
    /// The sub-agent's session ID (set on completion via `SubAgentComplete` event).
    pub session_id: Option<String>,
    /// The sub-agent's full conversation as UI messages (set on completion).
    pub session_messages: Vec<UiMessage>,
    /// Provider powering this sub-agent, if external.
    pub provider: Option<String>,
    /// Whether this sub-agent resumed a prior external session.
    pub resumed: bool,
    /// Delegated cost in USD once complete.
    pub cost_usd: Option<f64>,
    /// Total input tokens consumed by the sub-agent.
    pub input_tokens: Option<usize>,
    /// Total output tokens consumed by the sub-agent.
    pub output_tokens: Option<usize>,
}

#[derive(Debug, Clone)]
pub struct UiMessage {
    pub kind: MessageKind,
    pub content: String,
    pub is_streaming: bool,
    /// Model name for assistant messages (shown as metadata).
    pub model_name: Option<String>,
    /// Response time in seconds for assistant messages.
    pub response_time: Option<f64>,
    /// Sub-agent metadata (only set for `MessageKind::SubAgent`).
    pub sub_agent: Option<SubAgentData>,
    /// Tool name (for ToolCall/ToolResult messages).
    pub tool_name: Option<String>,
    /// Agent mode when this message was created.
    pub agent_mode: Option<String>,
    /// When the message started (for computing duration in footer).
    pub started_at: Option<std::time::Instant>,
    /// Transient messages are removed when the user sends a new message.
    /// Used for system info commands (/help, /queue, etc.) that should not
    /// pollute the chat history.
    pub transient: bool,
    /// Whether thinking content is expanded (show all lines) or collapsed (first 5 lines).
    /// Only meaningful for `MessageKind::Thinking` messages. Default: `false` (collapsed).
    pub thinking_expanded: bool,
    /// Whether this message was cancelled by the user pressing Esc.
    /// Cancelled tool calls render dimmed with `[interrupted]` suffix.
    pub cancelled: bool,
    /// Whether the action group containing this tool message is expanded.
    /// Only meaningful for `MessageKind::ToolCall` / `MessageKind::ToolResult`.
    /// When toggled on any message in a group, the renderer checks all group
    /// members and uses this flag for per-group expand/collapse.
    pub tool_group_expanded: bool,
}

impl UiMessage {
    pub fn new(kind: MessageKind, content: impl Into<String>) -> Self {
        Self {
            kind,
            content: content.into(),
            is_streaming: false,
            model_name: None,
            response_time: None,
            sub_agent: None,
            tool_name: None,
            agent_mode: None,
            started_at: None,
            transient: false,
            thinking_expanded: false,
            cancelled: false,
            tool_group_expanded: false,
        }
    }

    /// Create a transient message that will be removed when the user sends
    /// their next message. Ideal for info-only output (/help, /queue, etc.).
    pub fn transient(kind: MessageKind, content: impl Into<String>) -> Self {
        let mut msg = Self::new(kind, content);
        msg.transient = true;
        msg
    }
}
