# ava-session

> Session persistence with SQLite storage, FTS5 search, and conversation trees.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `SessionManager` | Primary interface for session CRUD |
| `SessionManager::new(path)` | Initialize database with schema |
| `SessionManager::create()` | Create new Session object |
| `SessionManager::save(session)` | Persist session with messages |
| `SessionManager::get(id)` | Load session by UUID |
| `SessionManager::list_recent(limit)` | Recently updated sessions |
| `SessionManager::search(query)` | FTS5 full-text search over messages |
| `SessionManager::fork(session)` | Clone session with parent_id metadata |
| `SessionManager::delete(id)` | Remove session and messages |
| `SessionManager::rename(id, title)` | Update metadata title |
| `SessionManager::get_children(parent_id)` | List child sessions |
| `SessionManager::db_path()` | Get database path |
| `SessionManager::save_incremental(...)` | Update without deleting other branches |
| `SessionManager::backfill_parent_ids()` | Migrate legacy linear sessions |
| `SessionManager::add_bookmark(...)` | Create bookmark at message index |
| `SessionManager::list_bookmarks(session_id)` | All bookmarks for session |
| `SessionManager::remove_bookmark(id)` | Delete bookmark |
| `SessionManager::clear_bookmarks(session_id)` | Delete all bookmarks for session |
| `SessionManager::get_branch(session_id, leaf_id)` | Messages from root to leaf |
| `SessionManager::get_tree(session_id)` | Full ConversationTree |
| `SessionManager::get_branch_leaves(session_id)` | All leaf nodes with preview |
| `SessionManager::branch_from(session_id, point, message)` | Create new branch |
| `SessionManager::switch_branch(session_id, leaf_id)` | Set active branch head |
| `generate_title(first_message)` | Heuristic title from first user message |
| `Bookmark` | Struct: id, session_id, label, message_index, created_at |
| `ConversationTree` | Struct: root, nodes HashMap, branch_head |
| `TreeNode` | Struct: message, children Vec |
| `BranchLeaf` | Struct: leaf_id, preview, depth, role, timestamp, is_active |
| `SessionDiffStats` | Struct: files_changed, additions, deletions |
| `compute_session_diff()` | Diff against HEAD |
| `compute_diff_against(git_ref)` | Diff against specific ref |
| `compute_staged_diff()` | Diff staged changes only |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | SessionManager with 1300+ lines, Bookmark/Tree/Branch types, generate_title |
| `helpers.rs` | SCHEMA_SQL, MIGRATION_SQL, role conversion, UUID/datetime parsing |
| `diff_tracking.rs` | Git diff stat parsing, SessionDiffStats |
| `tests/session_manager.rs` | Integration tests |

## Dependencies

Uses: ava-types
Used by: ava-tui, ava-agent

## Key Patterns

- Uses `rusqlite` (sync) with WAL mode, foreign keys, 5s busy timeout
- Schema versioning via `PRAGMA user_version` + MIGRATION_SQL array
- Message parent_id links enable conversation trees (branching)
- FTS5 virtual table for full-text search with auto-update triggers
- Transactions for multi-table operations
- Bookmarks have cascade delete FK to sessions
- `generate_title()` truncates at 50 chars on word boundary
- Diff stats via `git diff --numstat` parsing
- Branch head tracking for active conversation path
- Incremental save preserves messages from other branches
