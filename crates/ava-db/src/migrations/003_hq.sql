-- Historical HQ compatibility migration.
--
-- AVA 3.3 no longer uses these tables in the default core product surface, but
-- they remain so existing databases can still migrate cleanly.

CREATE TABLE IF NOT EXISTS hq_epics (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    description TEXT NOT NULL,
    status TEXT NOT NULL,
    progress INTEGER NOT NULL DEFAULT 0,
    plan_id TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS hq_issues (
    id TEXT PRIMARY KEY,
    issue_number INTEGER NOT NULL UNIQUE,
    identifier TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL,
    description TEXT NOT NULL,
    status TEXT NOT NULL,
    priority TEXT NOT NULL,
    assignee_id TEXT,
    assignee_name TEXT,
    epic_id TEXT NOT NULL,
    phase_label TEXT,
    agent_turn INTEGER,
    agent_max_turns INTEGER,
    agent_live_action TEXT,
    is_live INTEGER NOT NULL DEFAULT 0,
    files_changed_json TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (epic_id) REFERENCES hq_epics(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS hq_comments (
    id TEXT PRIMARY KEY,
    issue_id TEXT NOT NULL,
    author_name TEXT NOT NULL,
    author_role TEXT NOT NULL,
    author_icon TEXT,
    content TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    FOREIGN KEY (issue_id) REFERENCES hq_issues(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS hq_plans (
    id TEXT PRIMARY KEY,
    epic_id TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL,
    status TEXT NOT NULL,
    director_description TEXT NOT NULL,
    plan_json TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (epic_id) REFERENCES hq_epics(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS hq_agents (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    role TEXT NOT NULL,
    tier TEXT NOT NULL,
    model TEXT NOT NULL,
    status TEXT NOT NULL,
    icon TEXT NOT NULL,
    parent_id TEXT,
    current_task TEXT,
    current_issue_id TEXT,
    turn INTEGER,
    max_turns INTEGER,
    assigned_issue_ids_json TEXT,
    files_touched_json TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS hq_agent_transcript (
    id TEXT PRIMARY KEY,
    agent_id TEXT NOT NULL,
    entry_type TEXT NOT NULL,
    tool_name TEXT,
    tool_path TEXT,
    tool_status TEXT,
    content TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    FOREIGN KEY (agent_id) REFERENCES hq_agents(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS hq_activity (
    id TEXT PRIMARY KEY,
    event_type TEXT NOT NULL,
    agent_name TEXT,
    message TEXT NOT NULL,
    color TEXT NOT NULL,
    timestamp INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS hq_chat_messages (
    id TEXT PRIMARY KEY,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    delegations_json TEXT,
    epic_id TEXT,
    timestamp INTEGER NOT NULL,
    FOREIGN KEY (epic_id) REFERENCES hq_epics(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_hq_epics_created_at ON hq_epics(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_hq_issues_epic_id ON hq_issues(epic_id);
CREATE INDEX IF NOT EXISTS idx_hq_issues_status ON hq_issues(status);
CREATE INDEX IF NOT EXISTS idx_hq_comments_issue_id ON hq_comments(issue_id, timestamp);
CREATE INDEX IF NOT EXISTS idx_hq_plans_epic_id ON hq_plans(epic_id);
CREATE INDEX IF NOT EXISTS idx_hq_agents_parent_id ON hq_agents(parent_id);
CREATE INDEX IF NOT EXISTS idx_hq_agent_transcript_agent_id ON hq_agent_transcript(agent_id, timestamp);
CREATE INDEX IF NOT EXISTS idx_hq_activity_timestamp ON hq_activity(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_hq_chat_timestamp ON hq_chat_messages(timestamp ASC);
