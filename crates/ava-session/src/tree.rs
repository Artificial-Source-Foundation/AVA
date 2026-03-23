//! Conversation tree operations: branching, tree traversal, and parent-id backfill.

use std::collections::HashMap;

use ava_types::{AvaError, Message, Result};
use chrono::Utc;
use rusqlite::{params, Connection, OptionalExtension};
use uuid::Uuid;

use crate::helpers::{
    db_error, parse_datetime, parse_uuid, role_to_str, str_to_role, to_conversion_error,
};
use crate::{BranchLeaf, ConversationTree, SessionManager, TreeNode};

impl SessionManager {
    /// Get a linear branch by walking parent_id links from `leaf_id` back to root.
    /// Returns messages in chronological order (root first).
    pub fn get_branch(&self, session_id: Uuid, leaf_id: Uuid) -> Result<Vec<Message>> {
        let conn = self.open_conn()?;
        // Load all messages for this session into a HashMap
        let all_messages = self.load_all_messages(&conn, session_id)?;

        let mut branch = Vec::new();
        let mut current_id = Some(leaf_id);

        while let Some(id) = current_id {
            let msg = all_messages
                .get(&id)
                .ok_or_else(|| AvaError::NotFound(format!("message {id} not found in session")))?;
            branch.push(msg.clone());
            current_id = msg.parent_id;
        }

        branch.reverse(); // root first
        Ok(branch)
    }

    /// Get the full conversation tree for a session.
    pub fn get_tree(&self, session_id: Uuid) -> Result<ConversationTree> {
        let conn = self.open_conn()?;
        let all_messages = self.load_all_messages(&conn, session_id)?;

        let mut nodes: HashMap<Uuid, TreeNode> = HashMap::new();
        let mut roots = Vec::new();

        // Build tree nodes
        for (id, msg) in &all_messages {
            nodes.insert(
                *id,
                TreeNode {
                    message: msg.clone(),
                    children: Vec::new(),
                },
            );
        }

        // Wire up children
        for msg in all_messages.values() {
            if let Some(pid) = msg.parent_id {
                if let Some(parent_node) = nodes.get_mut(&pid) {
                    parent_node.children.push(msg.id);
                }
            } else {
                roots.push(msg.id);
            }
        }

        // Sort children by timestamp for deterministic ordering
        for node in nodes.values_mut() {
            node.children.sort_by(|a, b| {
                let ta = all_messages.get(a).map(|m| m.timestamp);
                let tb = all_messages.get(b).map(|m| m.timestamp);
                ta.cmp(&tb)
            });
        }

        // Pick the first root (there should be exactly one for well-formed sessions)
        let root = roots.into_iter().next();

        // Read branch_head from session
        let branch_head: Option<String> = conn
            .query_row(
                "SELECT branch_head FROM sessions WHERE id = ?1",
                params![session_id.to_string()],
                |row| row.get(0),
            )
            .optional()
            .map_err(db_error)?
            .flatten();

        let branch_head = branch_head.as_deref().map(parse_uuid).transpose()?;

        Ok(ConversationTree {
            root,
            nodes,
            branch_head,
        })
    }

    /// Get all leaf messages (messages with no children) for branch selection.
    pub fn get_branch_leaves(&self, session_id: Uuid) -> Result<Vec<BranchLeaf>> {
        let tree = self.get_tree(session_id)?;
        let mut leaves = Vec::new();

        for (id, node) in &tree.nodes {
            if node.children.is_empty() {
                // Count depth (branch length)
                let mut depth = 0;
                let mut cur = Some(*id);
                while let Some(cid) = cur {
                    depth += 1;
                    cur = tree.nodes.get(&cid).and_then(|n| n.message.parent_id);
                }

                let preview = if node.message.content.len() > 80 {
                    format!("{}...", &node.message.content[..77])
                } else {
                    node.message.content.clone()
                };

                leaves.push(BranchLeaf {
                    leaf_id: *id,
                    preview,
                    depth,
                    role: node.message.role.clone(),
                    timestamp: node.message.timestamp,
                    is_active: tree.branch_head == Some(*id),
                });
            }
        }

        leaves.sort_by(|a, b| a.timestamp.cmp(&b.timestamp));
        Ok(leaves)
    }

    /// Create a new branch from a specific message in the conversation.
    /// The new user message becomes a child of `branch_point_id`.
    pub fn branch_from(
        &self,
        session_id: Uuid,
        branch_point_id: Uuid,
        new_user_message: &str,
    ) -> Result<Message> {
        let mut conn = self.open_conn()?;

        let msg =
            Message::new(ava_types::Role::User, new_user_message).with_parent(branch_point_id);

        let tx = conn.transaction().map_err(db_error)?;

        tx.execute(
            "INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            params![
                msg.id.to_string(),
                session_id.to_string(),
                role_to_str(&msg.role),
                msg.content,
                msg.timestamp.to_rfc3339(),
                "[]",
                "[]",
                Option::<String>::None,
                "[]",
                msg.parent_id.map(|id| id.to_string()),
            ],
        )
        .map_err(db_error)?;

        // Update branch_head to the new message
        tx.execute(
            "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3",
            params![
                msg.id.to_string(),
                Utc::now().to_rfc3339(),
                session_id.to_string(),
            ],
        )
        .map_err(db_error)?;

        tx.commit().map_err(db_error)?;

        Ok(msg)
    }

    /// Switch the active branch to a different leaf.
    pub fn switch_branch(&self, session_id: Uuid, leaf_id: Uuid) -> Result<()> {
        let conn = self.open_conn()?;
        let updated = conn
            .execute(
                "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3",
                params![
                    leaf_id.to_string(),
                    Utc::now().to_rfc3339(),
                    session_id.to_string(),
                ],
            )
            .map_err(db_error)?;

        if updated == 0 {
            return Err(AvaError::NotFound(format!(
                "session {session_id} not found"
            )));
        }
        Ok(())
    }

    /// Backfill parent_id for existing linear sessions.
    /// Chains messages by timestamp order within each session.
    pub fn backfill_parent_ids(&self) -> Result<usize> {
        let conn = self.open_conn()?;

        // Find sessions with messages that have NULL parent_id
        let mut stmt = conn
            .prepare("SELECT DISTINCT session_id FROM messages WHERE parent_id IS NULL")
            .map_err(db_error)?;

        let session_ids: Vec<String> = stmt
            .query_map([], |row| row.get::<_, String>(0))
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut total_updated = 0;

        for sid in &session_ids {
            // Get messages ordered by timestamp
            let mut msg_stmt = conn
                .prepare(
                    "SELECT id FROM messages WHERE session_id = ?1 ORDER BY timestamp ASC, id ASC",
                )
                .map_err(db_error)?;

            let ids: Vec<String> = msg_stmt
                .query_map(params![sid], |row| row.get::<_, String>(0))
                .map_err(db_error)?
                .collect::<std::result::Result<Vec<_>, _>>()
                .map_err(db_error)?;

            // Chain: message[i].parent_id = message[i-1].id
            for i in 1..ids.len() {
                conn.execute(
                    "UPDATE messages SET parent_id = ?1 WHERE id = ?2",
                    params![ids[i - 1], ids[i]],
                )
                .map_err(db_error)?;
                total_updated += 1;
            }
        }

        Ok(total_updated)
    }

    /// Load all messages for a session into a HashMap keyed by message ID.
    pub(crate) fn load_all_messages(
        &self,
        conn: &Connection,
        session_id: Uuid,
    ) -> Result<HashMap<Uuid, Message>> {
        let mut stmt = conn
            .prepare(
                "SELECT id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id
                 FROM messages WHERE session_id = ?1",
            )
            .map_err(db_error)?;

        let messages = stmt
            .query_map(params![session_id.to_string()], |row| {
                let tool_calls =
                    serde_json::from_str::<Vec<ava_types::ToolCall>>(&row.get::<_, String>(4)?)
                        .map_err(|error| {
                            rusqlite::Error::FromSqlConversionFailure(
                                4,
                                rusqlite::types::Type::Text,
                                Box::new(error),
                            )
                        })?;
                let tool_results =
                    serde_json::from_str::<Vec<ava_types::ToolResult>>(&row.get::<_, String>(5)?)
                        .map_err(|error| {
                        rusqlite::Error::FromSqlConversionFailure(
                            5,
                            rusqlite::types::Type::Text,
                            Box::new(error),
                        )
                    })?;
                let tool_call_id: Option<String> = row.get(6)?;
                let images_json: String = row
                    .get::<_, Option<String>>(7)?
                    .unwrap_or_else(|| "[]".to_string());
                let images = serde_json::from_str::<Vec<ava_types::ImageContent>>(&images_json)
                    .map_err(|error| {
                        rusqlite::Error::FromSqlConversionFailure(
                            7,
                            rusqlite::types::Type::Text,
                            Box::new(error),
                        )
                    })?;
                let parent_id_str: Option<String> = row.get(8)?;
                let parent_id = parent_id_str
                    .as_deref()
                    .map(parse_uuid)
                    .transpose()
                    .map_err(to_conversion_error)?;

                Ok(Message {
                    id: parse_uuid(&row.get::<_, String>(0)?).map_err(to_conversion_error)?,
                    role: str_to_role(&row.get::<_, String>(1)?).map_err(to_conversion_error)?,
                    content: row.get(2)?,
                    timestamp: parse_datetime(&row.get::<_, String>(3)?)
                        .map_err(to_conversion_error)?,
                    tool_calls,
                    tool_results,
                    tool_call_id,
                    images,
                    parent_id,
                    agent_visible: true,
                    user_visible: true,
                    original_content: None,
                })
            })
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut map = HashMap::new();
        for msg in messages {
            map.insert(msg.id, msg);
        }
        Ok(map)
    }
}

#[cfg(test)]
mod tree_tests {
    use crate::*;
    use ava_types::{Message, Role, Session};
    use uuid::Uuid;

    fn temp_manager() -> (SessionManager, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        let mgr = SessionManager::new(dir.path().join("test.db")).unwrap();
        (mgr, dir)
    }

    /// Build a linear session with parent_id links: msg1 -> msg2 -> msg3.
    fn build_linear_session(mgr: &SessionManager) -> (Session, Vec<Uuid>) {
        let mut session = mgr.create().unwrap();

        let m1 = Message::new(Role::User, "Hello");
        let m1_id = m1.id;
        session.add_message(m1);

        let m2 = Message::new(Role::Assistant, "Hi there!").with_parent(m1_id);
        let m2_id = m2.id;
        session.add_message(m2);

        let m3 = Message::new(Role::User, "How are you?").with_parent(m2_id);
        let m3_id = m3.id;
        session.add_message(m3);

        session.branch_head = Some(m3_id);
        mgr.save(&session).unwrap();
        (session, vec![m1_id, m2_id, m3_id])
    }

    #[test]
    fn get_branch_returns_linear_path() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        let branch = mgr.get_branch(session.id, ids[2]).unwrap();
        assert_eq!(branch.len(), 3);
        assert_eq!(branch[0].id, ids[0]); // root first
        assert_eq!(branch[1].id, ids[1]);
        assert_eq!(branch[2].id, ids[2]);
    }

    #[test]
    fn get_branch_partial() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Get branch from midpoint
        let branch = mgr.get_branch(session.id, ids[1]).unwrap();
        assert_eq!(branch.len(), 2);
        assert_eq!(branch[0].id, ids[0]);
        assert_eq!(branch[1].id, ids[1]);
    }

    #[test]
    fn get_tree_structure() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        let tree = mgr.get_tree(session.id).unwrap();
        assert_eq!(tree.root, Some(ids[0]));
        assert_eq!(tree.nodes.len(), 3);
        assert_eq!(tree.branch_head, Some(ids[2]));

        // Check parent-child relationships
        let root_node = tree.nodes.get(&ids[0]).unwrap();
        assert_eq!(root_node.children, vec![ids[1]]);

        let mid_node = tree.nodes.get(&ids[1]).unwrap();
        assert_eq!(mid_node.children, vec![ids[2]]);

        let leaf_node = tree.nodes.get(&ids[2]).unwrap();
        assert!(leaf_node.children.is_empty());
    }

    #[test]
    fn branch_from_creates_fork() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork from the assistant message (ids[1])
        let new_msg = mgr
            .branch_from(session.id, ids[1], "Actually, different question")
            .unwrap();

        assert_eq!(new_msg.parent_id, Some(ids[1]));
        assert_eq!(new_msg.role, Role::User);

        // Tree should now have 4 nodes
        let tree = mgr.get_tree(session.id).unwrap();
        assert_eq!(tree.nodes.len(), 4);

        // ids[1] should have two children: ids[2] and new_msg.id
        let mid_node = tree.nodes.get(&ids[1]).unwrap();
        assert_eq!(mid_node.children.len(), 2);
        assert!(mid_node.children.contains(&ids[2]));
        assert!(mid_node.children.contains(&new_msg.id));

        // Branch head should be updated to new message
        assert_eq!(tree.branch_head, Some(new_msg.id));
    }

    #[test]
    fn get_branch_leaves_finds_all_leaves() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork to create two branches
        let fork_msg = mgr.branch_from(session.id, ids[1], "Branch B").unwrap();

        let leaves = mgr.get_branch_leaves(session.id).unwrap();
        assert_eq!(leaves.len(), 2);

        let leaf_ids: Vec<Uuid> = leaves.iter().map(|l| l.leaf_id).collect();
        assert!(leaf_ids.contains(&ids[2])); // original branch leaf
        assert!(leaf_ids.contains(&fork_msg.id)); // new branch leaf

        // The new branch should be marked active
        let active = leaves.iter().find(|l| l.is_active).unwrap();
        assert_eq!(active.leaf_id, fork_msg.id);
    }

    #[test]
    fn switch_branch_updates_head() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork
        let _fork = mgr.branch_from(session.id, ids[1], "Branch B").unwrap();

        // Switch back to original branch
        mgr.switch_branch(session.id, ids[2]).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.branch_head, Some(ids[2]));
    }

    #[test]
    fn save_incremental_preserves_other_branches() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork
        let fork_msg = mgr.branch_from(session.id, ids[1], "Branch B").unwrap();

        // Add new messages incrementally to the fork branch
        let m4 = Message::new(Role::Assistant, "Branch B reply").with_parent(fork_msg.id);
        let m4_id = m4.id;

        mgr.save_incremental(
            session.id,
            &[m4],
            &ava_types::TokenUsage::default(),
            Some(m4_id),
        )
        .unwrap();

        // All 5 messages should exist
        let tree = mgr.get_tree(session.id).unwrap();
        assert_eq!(tree.nodes.len(), 5);
        assert_eq!(tree.branch_head, Some(m4_id));

        // Original branch still intact
        let branch_a = mgr.get_branch(session.id, ids[2]).unwrap();
        assert_eq!(branch_a.len(), 3);

        // New branch
        let branch_b = mgr.get_branch(session.id, m4_id).unwrap();
        assert_eq!(branch_b.len(), 4); // root + assistant + fork_user + fork_reply
    }

    #[test]
    fn backfill_parent_ids_chains_linear_messages() {
        let (mgr, _dir) = temp_manager();
        let mut session = mgr.create().unwrap();

        // Add messages WITHOUT parent_id (simulating legacy data)
        let m1 = Message::new(Role::User, "First");
        let m1_id = m1.id;
        session.add_message(m1);

        let m2 = Message::new(Role::Assistant, "Second");
        let m2_id = m2.id;
        session.add_message(m2);

        let m3 = Message::new(Role::User, "Third");
        let _m3_id = m3.id;
        session.add_message(m3);

        mgr.save(&session).unwrap();

        // Verify parent_ids are NULL
        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert!(loaded.messages[0].parent_id.is_none());
        assert!(loaded.messages[1].parent_id.is_none());
        assert!(loaded.messages[2].parent_id.is_none());

        // Backfill
        let count = mgr.backfill_parent_ids().unwrap();
        assert_eq!(count, 2); // 2 messages get parent_id set

        // Verify chain
        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert!(loaded.messages[0].parent_id.is_none()); // root
        assert_eq!(loaded.messages[1].parent_id, Some(m1_id));
        assert_eq!(loaded.messages[2].parent_id, Some(m2_id));
    }

    #[test]
    fn branch_head_persists_through_save_load() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.branch_head, Some(ids[2]));
    }

    #[test]
    fn empty_session_tree() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let tree = mgr.get_tree(session.id).unwrap();
        assert!(tree.root.is_none());
        assert!(tree.nodes.is_empty());
        assert!(tree.branch_head.is_none());

        let leaves = mgr.get_branch_leaves(session.id).unwrap();
        assert!(leaves.is_empty());
    }
}
