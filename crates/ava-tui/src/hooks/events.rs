use serde::{Deserialize, Serialize};

/// Lifecycle events that hooks can listen for.
///
/// AVA supports 16 hook events spanning tool, session, agent, and system
/// lifecycles. Four of these (PreModelSwitch, PostModelSwitch, BudgetWarning,
/// UserPromptSubmit) are AVA-specific and have no counterpart in competing
/// tools.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum HookEvent {
    // ── Tool lifecycle ──────────────────────────────────────────────
    /// Fires before a tool executes. A hook returning exit code 2
    /// (or `HookResult::Block`) will prevent the tool from running.
    PreToolUse,
    /// Fires after a tool completes successfully.
    PostToolUse,
    /// Fires after a tool execution fails.
    PostToolUseFailure,

    // ── Session lifecycle ───────────────────────────────────────────
    /// Fires when a session begins or is resumed.
    SessionStart,
    /// Fires when a session terminates.
    SessionEnd,

    // ── Agent lifecycle ─────────────────────────────────────────────
    /// Fires when the agent finishes responding.
    Stop,
    /// Fires when a sub-agent is spawned.
    SubagentStart,
    /// Fires when a sub-agent finishes.
    SubagentStop,

    // ── System events ───────────────────────────────────────────────
    /// Fires when the agent needs user attention.
    Notification,
    /// Fires when configuration files are changed.
    ConfigChange,
    /// Fires before context window compaction.
    PreCompact,
    /// Fires when a permission dialog is shown.
    PermissionRequest,

    // ── AVA-specific events ─────────────────────────────────────────
    /// Fires before a model or provider switch.
    PreModelSwitch,
    /// Fires after a model or provider switch completes.
    PostModelSwitch,
    /// Fires when a token or cost budget threshold is reached.
    BudgetWarning,
    /// Fires when the user submits a prompt.
    UserPromptSubmit,
}

impl HookEvent {
    /// Parse a string into a HookEvent (case-insensitive).
    pub fn from_str_loose(s: &str) -> Option<Self> {
        match s.to_lowercase().replace(['-', '_'], "").as_str() {
            "pretooluse" => Some(Self::PreToolUse),
            "posttooluse" => Some(Self::PostToolUse),
            "posttoolusefailure" => Some(Self::PostToolUseFailure),
            "sessionstart" => Some(Self::SessionStart),
            "sessionend" => Some(Self::SessionEnd),
            "stop" => Some(Self::Stop),
            "subagentstart" => Some(Self::SubagentStart),
            "subagentstop" => Some(Self::SubagentStop),
            "notification" => Some(Self::Notification),
            "configchange" => Some(Self::ConfigChange),
            "precompact" => Some(Self::PreCompact),
            "permissionrequest" => Some(Self::PermissionRequest),
            "premodelswitch" => Some(Self::PreModelSwitch),
            "postmodelswitch" => Some(Self::PostModelSwitch),
            "budgetwarning" => Some(Self::BudgetWarning),
            "userpromptsubmit" => Some(Self::UserPromptSubmit),
            _ => None,
        }
    }

    /// Display label for UI.
    pub fn label(&self) -> &'static str {
        match self {
            Self::PreToolUse => "PreToolUse",
            Self::PostToolUse => "PostToolUse",
            Self::PostToolUseFailure => "PostToolUseFailure",
            Self::SessionStart => "SessionStart",
            Self::SessionEnd => "SessionEnd",
            Self::Stop => "Stop",
            Self::SubagentStart => "SubagentStart",
            Self::SubagentStop => "SubagentStop",
            Self::Notification => "Notification",
            Self::ConfigChange => "ConfigChange",
            Self::PreCompact => "PreCompact",
            Self::PermissionRequest => "PermissionRequest",
            Self::PreModelSwitch => "PreModelSwitch",
            Self::PostModelSwitch => "PostModelSwitch",
            Self::BudgetWarning => "BudgetWarning",
            Self::UserPromptSubmit => "UserPromptSubmit",
        }
    }

    /// All event variants, for iteration.
    pub fn all() -> &'static [HookEvent] {
        &[
            Self::PreToolUse,
            Self::PostToolUse,
            Self::PostToolUseFailure,
            Self::SessionStart,
            Self::SessionEnd,
            Self::Stop,
            Self::SubagentStart,
            Self::SubagentStop,
            Self::Notification,
            Self::ConfigChange,
            Self::PreCompact,
            Self::PermissionRequest,
            Self::PreModelSwitch,
            Self::PostModelSwitch,
            Self::BudgetWarning,
            Self::UserPromptSubmit,
        ]
    }
}

impl std::fmt::Display for HookEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.label())
    }
}

/// Contextual data passed to hooks when they fire.
///
/// All fields are optional; only the fields relevant to the specific event
/// will be populated (e.g., `tool_name` is set for tool lifecycle events,
/// `model` and `provider` are set for model switch events).
#[derive(Debug, Clone, Default, Serialize)]
pub struct HookContext {
    /// The event that triggered this hook.
    pub event: String,
    /// Tool name (for tool lifecycle events).
    pub tool_name: Option<String>,
    /// Tool input arguments as JSON (for PreToolUse).
    pub tool_input: Option<serde_json::Value>,
    /// Tool output content (for PostToolUse/PostToolUseFailure).
    pub tool_output: Option<String>,
    /// File path involved (for edit/write tools).
    pub file_path: Option<String>,
    /// Current model identifier.
    pub model: Option<String>,
    /// Current provider identifier.
    pub provider: Option<String>,
    /// Active session identifier.
    pub session_id: Option<String>,
    /// Total tokens used in this session.
    pub tokens_used: Option<usize>,
    /// Estimated cost in USD for the session.
    pub cost_usd: Option<f64>,
    /// The user's prompt text (for UserPromptSubmit).
    pub prompt: Option<String>,
    /// Sub-agent description (for SubagentStart/SubagentStop).
    pub subagent_description: Option<String>,
    /// Working directory.
    pub cwd: Option<String>,
}

/// Controls how much data is included in hook context payloads.
///
/// Hook contexts are passed to external commands and HTTP endpoints, which
/// makes them a potential exfiltration surface. The payload level limits
/// how much content is exposed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum HookPayloadLevel {
    /// Only event type, tool name, cwd.
    #[default]
    Minimal,
    /// Minimal + truncated content (first 500 chars).
    Standard,
    /// Everything — opt-in only.
    Full,
}

/// Maximum number of characters for truncated fields at Standard level.
const TRUNCATE_LIMIT: usize = 500;

impl HookContext {
    /// Create a minimal context for the given event.
    pub fn for_event(event: &HookEvent) -> Self {
        Self {
            event: event.label().to_string(),
            cwd: std::env::current_dir()
                .ok()
                .map(|p| p.display().to_string()),
            ..Default::default()
        }
    }

    /// Truncate large fields in the context to reduce exfiltration surface.
    ///
    /// By default, hook contexts use `HookPayloadLevel::Minimal`, which strips
    /// tool input/output, prompt text, and session metadata. `Standard` keeps
    /// them but truncates to 500 characters. `Full` passes everything as-is.
    pub fn apply_payload_level(&mut self, level: HookPayloadLevel) {
        match level {
            HookPayloadLevel::Full => { /* pass everything */ }
            HookPayloadLevel::Standard => {
                self.tool_input = self.tool_input.take().map(truncate_json_value);
                self.tool_output = self.tool_output.take().map(|s| truncate_string(&s));
                self.prompt = self.prompt.take().map(|s| truncate_string(&s));
            }
            HookPayloadLevel::Minimal => {
                // Strip all content fields — keep only event, tool_name, cwd
                self.tool_input = None;
                self.tool_output = None;
                self.prompt = None;
                self.session_id = None;
                self.cost_usd = None;
                self.tokens_used = None;
                self.subagent_description = None;
            }
        }
    }
}

fn truncate_string(s: &str) -> String {
    if s.len() > TRUNCATE_LIMIT {
        format!("{}...[truncated]", &s[..TRUNCATE_LIMIT])
    } else {
        s.to_string()
    }
}

fn truncate_json_value(v: serde_json::Value) -> serde_json::Value {
    let s = v.to_string();
    if s.len() > TRUNCATE_LIMIT {
        serde_json::Value::String(format!("{}...[truncated]", &s[..TRUNCATE_LIMIT]))
    } else {
        v
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_all_events() {
        for event in HookEvent::all() {
            let label = event.label();
            let parsed = HookEvent::from_str_loose(label);
            assert_eq!(parsed.as_ref(), Some(event), "Failed to round-trip {label}");
        }
    }

    #[test]
    fn parse_case_insensitive() {
        assert_eq!(
            HookEvent::from_str_loose("pretooluse"),
            Some(HookEvent::PreToolUse)
        );
        assert_eq!(
            HookEvent::from_str_loose("PRETOOLUSE"),
            Some(HookEvent::PreToolUse)
        );
        assert_eq!(
            HookEvent::from_str_loose("PreToolUse"),
            Some(HookEvent::PreToolUse)
        );
        assert_eq!(
            HookEvent::from_str_loose("pre_tool_use"),
            Some(HookEvent::PreToolUse)
        );
        assert_eq!(
            HookEvent::from_str_loose("pre-tool-use"),
            Some(HookEvent::PreToolUse)
        );
    }

    #[test]
    fn parse_unknown_returns_none() {
        assert_eq!(HookEvent::from_str_loose("nonexistent"), None);
        assert_eq!(HookEvent::from_str_loose(""), None);
    }

    #[test]
    fn context_serializes_to_json() {
        let ctx = HookContext {
            event: "PreToolUse".to_string(),
            tool_name: Some("edit".to_string()),
            tool_input: Some(serde_json::json!({"file_path": "src/main.rs"})),
            ..Default::default()
        };
        let json = serde_json::to_string(&ctx).unwrap();
        assert!(json.contains("PreToolUse"));
        assert!(json.contains("edit"));
        assert!(json.contains("src/main.rs"));
    }
}
