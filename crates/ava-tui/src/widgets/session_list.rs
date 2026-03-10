use ava_types::Session;
use uuid::Uuid;

use crate::widgets::select_list::{SelectItem, SelectListState};

#[derive(Debug)]
pub struct SessionListState {
    pub open: bool,
    pub list: SelectListState<Uuid>,
}

impl Default for SessionListState {
    fn default() -> Self {
        Self {
            open: false,
            list: SelectListState::new(Vec::new()),
        }
    }
}

impl SessionListState {
    pub fn update_sessions(&mut self, sessions: &[Session]) {
        let items = std::iter::once(SelectItem {
            title: "+ New Session".to_string(),
            detail: String::new(),
            section: Some("Actions".to_string()),
            status: None,
            value: Uuid::nil(),
            enabled: true,
        })
        .chain(sessions.iter().map(|s| {
            let title = session_title(s);
            let msg_count = s.messages.len();
            let relative = relative_date(&s.updated_at);
            let detail = format!(
                "{} msg{}  {}",
                msg_count,
                if msg_count == 1 { "" } else { "s" },
                relative,
            );
            SelectItem {
                title,
                detail,
                section: Some("Sessions".to_string()),
                status: None,
                value: s.id,
                enabled: true,
            }
        }))
        .collect();
        self.list.set_items(items);
    }
}

/// Format a datetime as a relative date string.
fn relative_date(dt: &chrono::DateTime<chrono::Utc>) -> String {
    let now = chrono::Utc::now();
    let diff = now.signed_duration_since(*dt);
    if diff.num_minutes() < 1 {
        "just now".to_string()
    } else if diff.num_minutes() < 60 {
        format!("{}m ago", diff.num_minutes())
    } else if diff.num_hours() < 24 {
        format!("{}h ago", diff.num_hours())
    } else if diff.num_days() < 7 {
        format!("{}d ago", diff.num_days())
    } else if diff.num_weeks() < 5 {
        format!("{}w ago", diff.num_weeks())
    } else {
        dt.format("%Y-%m-%d").to_string()
    }
}

/// Get the session title from metadata, or fall back to deriving one from the first user message.
fn session_title(session: &Session) -> String {
    // Check for a stored title in metadata first
    if let Some(title) = session
        .metadata
        .as_object()
        .and_then(|m| m.get("title"))
        .and_then(|v| v.as_str())
    {
        if !title.is_empty() {
            return title.to_string();
        }
    }

    // Backward compat: derive title from first user message
    let first_user = session
        .messages
        .iter()
        .find(|m| m.role == ava_types::Role::User);
    match first_user {
        Some(msg) => ava_session::generate_title(&msg.content),
        None => format!("Session {}", &session.id.to_string()[..8]),
    }
}
