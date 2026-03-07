use ava_types::Session;

#[derive(Debug, Default)]
pub struct SessionListState {
    pub open: bool,
    pub query: String,
    pub selected: usize,
}

pub fn filter_sessions<'a>(sessions: &'a [Session], query: &str) -> Vec<&'a Session> {
    if query.trim().is_empty() {
        return sessions.iter().collect();
    }
    let needle = query.to_lowercase();
    sessions
        .iter()
        .filter(|s| {
            s.id.to_string().contains(&needle)
                || s.metadata
                    .to_string()
                    .to_lowercase()
                    .contains(needle.as_str())
        })
        .collect()
}
