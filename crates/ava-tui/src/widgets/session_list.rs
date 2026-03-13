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
            let mut detail_parts = vec![format!(
                "{} msg{}",
                msg_count,
                if msg_count == 1 { "" } else { "s" }
            )];
            if let Some(cost) = session_cost_detail(s) {
                detail_parts.push(cost);
            }
            if let Some(route) = session_route_detail(s) {
                detail_parts.push(route);
            }
            detail_parts.push(relative);
            let detail = detail_parts.join("  ");
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

fn session_cost_detail(session: &Session) -> Option<String> {
    let summary = crate::session_summary::cost_summary(session)?;
    Some(match summary.budget_usd {
        Some(budget) if budget > 0.0 => format!("${:.2}/${budget:.2}", summary.total_usd),
        _ => format!("${:.2}", summary.total_usd),
    })
}

fn session_route_detail(session: &Session) -> Option<String> {
    crate::session_summary::route_summary(session).map(|summary| {
        summary
            .split(" via ")
            .next()
            .unwrap_or(summary.as_str())
            .to_string()
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role};

    #[test]
    fn session_details_include_cost_and_route_summary() {
        let mut state = SessionListState::default();
        let session = Session::new().with_metadata(serde_json::json!({
            "title": "Budget run",
            "costSummary": {
                "totalUsd": 0.42,
                "budgetUsd": 1.0
            },
            "routing": {
                "profile": "cheap"
            }
        }));

        state.update_sessions(&[session]);

        let detail = &state.list.items[1].detail;
        assert!(detail.contains("$0.42/$1.00"));
        assert!(detail.contains("cheap route"));
    }

    #[test]
    fn session_title_falls_back_to_first_user_message() {
        let mut session = Session::new();
        session.add_message(Message::new(Role::User, "Investigate budget alerts"));

        let title = session_title(&session);

        assert!(title.contains("Investigate budget alerts"));
    }
}
