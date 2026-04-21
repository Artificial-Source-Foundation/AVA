use std::collections::HashMap;
use std::env;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

use ava_types::{Message, Role};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use tracing::warn;

const MAX_STORED_SESSIONS: usize = 1000;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct StoredSession {
    agent_name: String,
    model_name: String,
    cwd: String,
    external_session_id: String,
    updated_at_ms: u64,
}

#[derive(Debug, Clone, Serialize)]
struct NormalizedMessage {
    role: &'static str,
    content: String,
    tool_calls: serde_json::Value,
    tool_results: serde_json::Value,
    tool_call_id: Option<String>,
    structured_content: serde_json::Value,
}

fn file_lock() -> &'static Mutex<()> {
    static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
    LOCK.get_or_init(|| Mutex::new(()))
}

pub fn lookup_session(
    agent_name: &str,
    model_name: &str,
    cwd: &str,
    messages: &[Message],
) -> Option<String> {
    lookup_session_at(None, agent_name, model_name, cwd, messages)
}

#[cfg(test)]
pub fn lookup_session_for_path(
    path: &Path,
    agent_name: &str,
    model_name: &str,
    cwd: &str,
    messages: &[Message],
) -> Option<String> {
    lookup_session_at(Some(path), agent_name, model_name, cwd, messages)
}

fn lookup_session_at(
    store_path_override: Option<&Path>,
    agent_name: &str,
    model_name: &str,
    cwd: &str,
    messages: &[Message],
) -> Option<String> {
    let _guard = file_lock()
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    let store = read_store(store_path_override);

    for prefix_len in (1..=messages.len()).rev() {
        let key = conversation_key(agent_name, model_name, cwd, &messages[..prefix_len]);
        let Some(entry) = store.get(&key) else {
            continue;
        };
        if entry.agent_name == agent_name && entry.model_name == model_name && entry.cwd == cwd {
            return Some(entry.external_session_id.clone());
        }
    }

    None
}

pub fn store_session(
    agent_name: &str,
    model_name: &str,
    cwd: &str,
    messages: &[Message],
    external_session_id: &str,
) {
    store_session_at(
        None,
        agent_name,
        model_name,
        cwd,
        messages,
        external_session_id,
    );
}

#[cfg(test)]
pub fn store_session_for_path(
    path: &Path,
    agent_name: &str,
    model_name: &str,
    cwd: &str,
    messages: &[Message],
    external_session_id: &str,
) {
    store_session_at(
        Some(path),
        agent_name,
        model_name,
        cwd,
        messages,
        external_session_id,
    );
}

fn store_session_at(
    store_path_override: Option<&Path>,
    agent_name: &str,
    model_name: &str,
    cwd: &str,
    messages: &[Message],
    external_session_id: &str,
) {
    let _guard = file_lock()
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    let mut store = read_store(store_path_override);
    let key = conversation_key(agent_name, model_name, cwd, messages);
    store.insert(
        key,
        StoredSession {
            agent_name: agent_name.to_string(),
            model_name: model_name.to_string(),
            cwd: cwd.to_string(),
            external_session_id: external_session_id.to_string(),
            updated_at_ms: now_ms(),
        },
    );

    if store.len() > MAX_STORED_SESSIONS {
        let mut entries: Vec<_> = store.into_iter().collect();
        entries.sort_by_key(|(_, entry)| entry.updated_at_ms);
        entries.drain(0..entries.len().saturating_sub(MAX_STORED_SESSIONS));
        store = entries.into_iter().collect();
    }

    write_store(store_path_override, &store);
}

fn conversation_key(agent_name: &str, model_name: &str, cwd: &str, messages: &[Message]) -> String {
    let normalized: Vec<NormalizedMessage> = messages
        .iter()
        .filter(|message| message.agent_visible)
        .map(|message| NormalizedMessage {
            role: match message.role {
                Role::System => "system",
                Role::User => "user",
                Role::Assistant => "assistant",
                Role::Tool => "tool",
            },
            content: message.content.clone(),
            tool_calls: serde_json::to_value(&message.tool_calls).unwrap_or_default(),
            tool_results: serde_json::to_value(&message.tool_results).unwrap_or_default(),
            tool_call_id: message.tool_call_id.clone(),
            structured_content: serde_json::to_value(&message.structured_content)
                .unwrap_or_default(),
        })
        .collect();

    let payload = serde_json::json!({
        "agent": agent_name,
        "model": model_name,
        "cwd": cwd,
        "messages": normalized,
    });
    let json = serde_json::to_vec(&payload).unwrap_or_default();
    let mut hasher = Sha256::new();
    hasher.update(json);
    format!("{:x}", hasher.finalize())
}

fn read_store(store_path_override: Option<&Path>) -> HashMap<String, StoredSession> {
    let path = store_path(store_path_override);
    if !path.exists() {
        return HashMap::new();
    }

    std::fs::read_to_string(&path)
        .ok()
        .and_then(|content| serde_json::from_str(&content).ok())
        .unwrap_or_default()
}

fn write_store(store_path_override: Option<&Path>, store: &HashMap<String, StoredSession>) {
    let path = store_path(store_path_override);
    if let Some(parent) = path.parent() {
        if let Err(error) = std::fs::create_dir_all(parent) {
            warn!(path = %parent.display(), %error, "failed to create ACP session-store directory");
            return;
        }
    }
    let tmp = path.with_extension("json.tmp");
    let content = match serde_json::to_string_pretty(store) {
        Ok(content) => content,
        Err(error) => {
            warn!(%error, "failed to serialize ACP session store");
            return;
        }
    };
    if let Err(error) = std::fs::write(&tmp, content) {
        warn!(path = %tmp.display(), %error, "failed to write ACP session-store temp file");
        return;
    }
    if let Err(error) = std::fs::rename(&tmp, &path) {
        warn!(from = %tmp.display(), to = %path.display(), %error, "failed to finalize ACP session-store write");
    }
}

fn store_path(store_path_override: Option<&Path>) -> PathBuf {
    if let Some(path) = store_path_override {
        return path.to_path_buf();
    }

    if let Ok(path) = env::var("AVA_ACP_SESSION_STORE") {
        return PathBuf::from(path);
    }

    if let Ok(state_home) = env::var("XDG_STATE_HOME") {
        return PathBuf::from(state_home)
            .join("ava")
            .join("acp-sessions.json");
    }

    if let Ok(home) = env::var("HOME") {
        return Path::new(&home)
            .join(".local")
            .join("state")
            .join("ava")
            .join("acp-sessions.json");
    }

    Path::new(".").join("acp-sessions.json")
}

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

#[cfg(test)]
mod tests {
    use super::*;

    fn message(role: Role, content: &str) -> Message {
        Message::new(role, content)
    }

    #[test]
    fn longest_prefix_lookup_resumes_prior_turn() {
        let dir = tempfile::tempdir().unwrap();
        let store_path = dir.path().join("sessions.json");

        let turn_one = vec![message(Role::System, "ctx"), message(Role::User, "first")];
        store_session_for_path(
            &store_path,
            "claude-code",
            "sonnet",
            "/tmp/project",
            &turn_one,
            "sess-1",
        );

        let turn_two = vec![
            message(Role::System, "ctx"),
            message(Role::User, "first"),
            message(Role::Assistant, "done"),
            message(Role::User, "second"),
        ];

        let resumed = lookup_session_for_path(
            &store_path,
            "claude-code",
            "sonnet",
            "/tmp/project",
            &turn_two,
        );
        assert_eq!(resumed.as_deref(), Some("sess-1"));
    }

    #[test]
    fn store_is_scoped_by_agent_model_and_cwd() {
        let dir = tempfile::tempdir().unwrap();
        let store_path = dir.path().join("sessions.json");

        let messages = vec![message(Role::User, "task")];
        store_session_for_path(
            &store_path,
            "claude-code",
            "sonnet",
            "/tmp/project",
            &messages,
            "sess-1",
        );

        assert!(lookup_session_for_path(
            &store_path,
            "claude-code",
            "opus",
            "/tmp/project",
            &messages
        )
        .is_none());
        assert!(lookup_session_for_path(
            &store_path,
            "claude-code",
            "sonnet",
            "/tmp/other",
            &messages
        )
        .is_none());
        assert!(
            lookup_session_for_path(&store_path, "codex", "sonnet", "/tmp/project", &messages)
                .is_none()
        );
    }
}
