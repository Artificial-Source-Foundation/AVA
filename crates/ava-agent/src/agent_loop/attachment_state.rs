//! Attachment delta tracking for MCP servers, skills, and memories.
//!
//! On each turn, computes what changed since the last announcement so only
//! deltas are injected into the context (not the full list every time).

use std::collections::BTreeSet;

/// Tracks what attachments (MCP servers, skills, memories) were last announced
/// to the LLM, so only changes need to be injected into subsequent turns.
#[derive(Debug, Default, Clone)]
pub struct AttachmentState {
    /// MCP server names last announced.
    mcp_servers: BTreeSet<String>,
    /// Skill names last announced.
    skills: BTreeSet<String>,
    /// Memory source names last announced.
    memories: BTreeSet<String>,
    /// Whether any announcement has been made (first turn sends full list).
    initialized: bool,
}

/// A delta report of what changed since the last announcement.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct AttachmentDelta {
    /// Items that are new since last announcement.
    pub added: Vec<String>,
    /// Items that were present before but are now gone.
    pub removed: Vec<String>,
}

impl AttachmentDelta {
    /// Whether there are any changes to report.
    pub fn is_empty(&self) -> bool {
        self.added.is_empty() && self.removed.is_empty()
    }

    /// Format all changes into human-readable context injection lines.
    pub fn format_lines(&self) -> Vec<String> {
        let mut lines = Vec::with_capacity(self.added.len() + self.removed.len());
        for item in &self.added {
            lines.push(item.clone());
        }
        for item in &self.removed {
            lines.push(item.clone());
        }
        lines
    }
}

impl AttachmentState {
    pub fn new() -> Self {
        Self::default()
    }

    /// Compute the delta for MCP servers, skills, and memories.
    ///
    /// On the first call (`!initialized`), all current items are reported as "added".
    /// On subsequent calls, only differences are reported.
    ///
    /// Returns `None` if there are no changes to report (after the first turn).
    pub fn compute_delta(
        &mut self,
        current_mcp_servers: &[String],
        current_skills: &[String],
        current_memories: &[String],
    ) -> Option<AttachmentDelta> {
        let new_mcp: BTreeSet<String> = current_mcp_servers.iter().cloned().collect();
        let new_skills: BTreeSet<String> = current_skills.iter().cloned().collect();
        let new_memories: BTreeSet<String> = current_memories.iter().cloned().collect();

        if !self.initialized {
            // First turn: announce everything
            self.initialized = true;
            self.mcp_servers = new_mcp.clone();
            self.skills = new_skills.clone();
            self.memories = new_memories.clone();

            let mut delta = AttachmentDelta::default();
            for name in &new_mcp {
                delta
                    .added
                    .push(format!("[MCP server '{}' connected]", name));
            }
            for name in &new_skills {
                delta.added.push(format!("[Skill '{}' loaded]", name));
            }
            for name in &new_memories {
                delta
                    .added
                    .push(format!("[Memory source '{}' active]", name));
            }

            if delta.is_empty() {
                return None;
            }
            return Some(delta);
        }

        // Subsequent turns: compute diff
        let mut delta = AttachmentDelta::default();

        // MCP servers
        for name in new_mcp.difference(&self.mcp_servers) {
            delta
                .added
                .push(format!("[MCP server '{}' connected]", name));
        }
        for name in self.mcp_servers.difference(&new_mcp) {
            delta
                .removed
                .push(format!("[MCP server '{}' disconnected]", name));
        }

        // Skills
        for name in new_skills.difference(&self.skills) {
            delta.added.push(format!("[Skill '{}' loaded]", name));
        }
        for name in self.skills.difference(&new_skills) {
            delta.removed.push(format!("[Skill '{}' unloaded]", name));
        }

        // Memories
        for name in new_memories.difference(&self.memories) {
            delta
                .added
                .push(format!("[Memory source '{}' active]", name));
        }
        for name in self.memories.difference(&new_memories) {
            delta
                .removed
                .push(format!("[Memory source '{}' inactive]", name));
        }

        // Update tracked state
        self.mcp_servers = new_mcp;
        self.skills = new_skills;
        self.memories = new_memories;

        if delta.is_empty() {
            None
        } else {
            Some(delta)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn first_turn_announces_full_list() {
        let mut state = AttachmentState::new();
        let delta = state
            .compute_delta(
                &["github".to_string(), "slack".to_string()],
                &["code-review".to_string()],
                &[],
            )
            .expect("first turn should produce a delta");

        assert_eq!(delta.added.len(), 3);
        assert!(delta.removed.is_empty());
        assert!(delta.added.iter().any(|s| s.contains("github")));
        assert!(delta.added.iter().any(|s| s.contains("slack")));
        assert!(delta.added.iter().any(|s| s.contains("code-review")));
    }

    #[test]
    fn second_turn_no_changes_returns_none() {
        let mut state = AttachmentState::new();
        let servers = vec!["github".to_string()];
        state.compute_delta(&servers, &[], &[]);

        // Same state, no changes
        let delta = state.compute_delta(&servers, &[], &[]);
        assert!(delta.is_none());
    }

    #[test]
    fn server_added_produces_delta() {
        let mut state = AttachmentState::new();
        state.compute_delta(&["github".to_string()], &[], &[]);

        // Add slack
        let delta = state
            .compute_delta(&["github".to_string(), "slack".to_string()], &[], &[])
            .expect("should detect new server");

        assert_eq!(delta.added.len(), 1);
        assert!(delta.added[0].contains("slack"));
        assert!(delta.added[0].contains("connected"));
        assert!(delta.removed.is_empty());
    }

    #[test]
    fn server_removed_produces_delta() {
        let mut state = AttachmentState::new();
        state.compute_delta(&["github".to_string(), "slack".to_string()], &[], &[]);

        // Remove slack
        let delta = state
            .compute_delta(&["github".to_string()], &[], &[])
            .expect("should detect removed server");

        assert!(delta.added.is_empty());
        assert_eq!(delta.removed.len(), 1);
        assert!(delta.removed[0].contains("slack"));
        assert!(delta.removed[0].contains("disconnected"));
    }

    #[test]
    fn empty_first_turn_returns_none() {
        let mut state = AttachmentState::new();
        let delta = state.compute_delta(&[], &[], &[]);
        assert!(delta.is_none());
    }

    #[test]
    fn format_lines_combines_added_and_removed() {
        let delta = AttachmentDelta {
            added: vec!["[MCP server 'github' connected]".to_string()],
            removed: vec!["[MCP server 'slack' disconnected]".to_string()],
        };
        let lines = delta.format_lines();
        assert_eq!(lines.len(), 2);
    }
}
