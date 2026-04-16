use ava_session::{Bookmark, SessionManager};
use ava_types::Session;
use color_eyre::eyre::{eyre, Result};
use std::path::Path;
use uuid::Uuid;

pub struct SessionState {
    manager: SessionManager,
    pub current_session: Option<Session>,
    pub sessions: Vec<Session>,
}

impl SessionState {
    pub fn new(db_path: &Path) -> Result<Self> {
        let manager = SessionManager::new(db_path)?;
        Ok(Self {
            manager,
            current_session: None,
            sessions: Vec::new(),
        })
    }

    pub fn create_session(&mut self) -> Result<Session> {
        let session = self.manager.create()?;
        self.current_session = Some(session.clone());
        Ok(session)
    }

    pub fn switch_to(&mut self, id: Uuid) -> Result<()> {
        self.current_session = self.manager.get(id)?;
        Ok(())
    }

    pub fn fork_current(&mut self) -> Result<Session> {
        let current = self
            .current_session
            .as_ref()
            .ok_or_else(|| eyre!("No current session to fork"))?;
        let forked = self.manager.fork(current)?;
        self.current_session = Some(forked.clone());
        Ok(forked)
    }

    pub fn list_recent(&mut self, limit: usize) -> Result<Vec<Session>> {
        let sessions = self.manager.list_recent(limit)?;
        self.sessions = sessions.clone();
        Ok(sessions)
    }

    pub fn search(&self, query: &str) -> Result<Vec<Session>> {
        Ok(self.manager.search(query)?)
    }

    pub fn db_path(&self) -> &Path {
        self.manager.db_path()
    }

    /// Save a completed agent session to the database.
    pub fn save_session(&mut self, session: &Session) {
        if let Err(e) = self.manager.save(session) {
            tracing::warn!("Failed to save session: {}", e);
        }
        self.current_session = Some(session.clone());
    }

    /// Incrementally persist messages without DELETE-all + INSERT-all.
    ///
    /// Used by the checkpoint handler to save progress crash-safely.
    pub fn checkpoint_session(&mut self, session: &Session) {
        if let Err(e) = self.manager.add_messages(session.id, &session.messages) {
            tracing::warn!("Failed to checkpoint session: {}", e);
        }
        self.current_session = Some(session.clone());
    }

    // ── Bookmark operations (BG-13) ──────────────────────────────────

    /// Add a bookmark at the given message index.
    pub fn add_bookmark(&self, label: &str, message_index: usize) -> Result<Bookmark> {
        let session = self
            .current_session
            .as_ref()
            .ok_or_else(|| eyre!("No active session"))?;
        Ok(self
            .manager
            .add_bookmark(session.id, label, message_index)?)
    }

    /// List bookmarks for the current session.
    pub fn list_bookmarks(&self) -> Result<Vec<Bookmark>> {
        let session = self
            .current_session
            .as_ref()
            .ok_or_else(|| eyre!("No active session"))?;
        Ok(self.manager.list_bookmarks(session.id)?)
    }

    /// Remove a bookmark by ID.
    pub fn remove_bookmark(&self, bookmark_id: Uuid) -> Result<()> {
        Ok(self.manager.remove_bookmark(bookmark_id)?)
    }

    /// Clear all bookmarks for the current session.
    pub fn clear_bookmarks(&self) -> Result<usize> {
        let session = self
            .current_session
            .as_ref()
            .ok_or_else(|| eyre!("No active session"))?;
        Ok(self.manager.clear_bookmarks(session.id)?)
    }
}

#[cfg(test)]
mod tests {
    use tempfile::tempdir;

    use super::*;

    #[test]
    fn checkpoint_session_refreshes_current_session_snapshot() {
        let temp = tempdir().expect("tempdir");
        let db_path = temp.path().join("sessions.db");
        let mut state = SessionState::new(&db_path).expect("session state");

        let mut session = Session::new();
        session.add_message(ava_types::Message::new(
            ava_types::Role::User,
            "checkpointed",
        ));

        state.checkpoint_session(&session);

        assert_eq!(
            state.current_session.as_ref().map(|s| s.id),
            Some(session.id)
        );
        assert_eq!(
            state
                .current_session
                .as_ref()
                .expect("current session")
                .messages,
            session.messages
        );
    }
}
