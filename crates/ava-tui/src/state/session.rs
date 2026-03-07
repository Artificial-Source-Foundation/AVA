use ava_session::SessionManager;
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

    /// Save a completed agent session to the database.
    pub fn save_session(&mut self, session: &Session) {
        if let Err(e) = self.manager.save(session) {
            tracing::warn!("Failed to save session: {}", e);
        }
        self.current_session = Some(session.clone());
    }
}
