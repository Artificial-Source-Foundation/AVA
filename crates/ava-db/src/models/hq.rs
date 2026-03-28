use sqlx::{FromRow, SqlitePool};

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqEpicRecord {
    pub id: String,
    pub title: String,
    pub description: String,
    pub status: String,
    pub progress: i64,
    pub plan_id: Option<String>,
    pub created_at: i64,
    pub updated_at: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqIssueRecord {
    pub id: String,
    pub issue_number: i64,
    pub identifier: String,
    pub title: String,
    pub description: String,
    pub status: String,
    pub priority: String,
    pub assignee_id: Option<String>,
    pub assignee_name: Option<String>,
    pub epic_id: String,
    pub phase_label: Option<String>,
    pub agent_turn: Option<i64>,
    pub agent_max_turns: Option<i64>,
    pub agent_live_action: Option<String>,
    pub is_live: i64,
    pub files_changed_json: Option<String>,
    pub created_at: i64,
    pub updated_at: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqCommentRecord {
    pub id: String,
    pub issue_id: String,
    pub author_name: String,
    pub author_role: String,
    pub author_icon: Option<String>,
    pub content: String,
    pub timestamp: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqPlanRecord {
    pub id: String,
    pub epic_id: String,
    pub title: String,
    pub status: String,
    pub director_description: String,
    pub plan_json: String,
    pub created_at: i64,
    pub updated_at: i64,
}

#[derive(Debug, Clone, PartialEq, FromRow)]
pub struct HqAgentRecord {
    pub id: String,
    pub name: String,
    pub role: String,
    pub tier: String,
    pub model: String,
    pub status: String,
    pub icon: String,
    pub parent_id: Option<String>,
    pub current_task: Option<String>,
    pub current_issue_id: Option<String>,
    pub turn: Option<i64>,
    pub max_turns: Option<i64>,
    pub assigned_issue_ids_json: Option<String>,
    pub files_touched_json: Option<String>,
    pub total_cost_usd: f64,
    pub created_at: i64,
    pub updated_at: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqAgentTranscriptRecord {
    pub id: String,
    pub agent_id: String,
    pub entry_type: String,
    pub tool_name: Option<String>,
    pub tool_path: Option<String>,
    pub tool_status: Option<String>,
    pub content: String,
    pub timestamp: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqActivityRecord {
    pub id: String,
    pub event_type: String,
    pub agent_name: Option<String>,
    pub message: String,
    pub color: String,
    pub timestamp: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct HqChatMessageRecord {
    pub id: String,
    pub role: String,
    pub content: String,
    pub delegations_json: Option<String>,
    pub epic_id: Option<String>,
    pub timestamp: i64,
}

#[derive(Debug, Clone)]
pub struct HqRepository {
    pool: SqlitePool,
}

impl HqRepository {
    pub fn new(pool: SqlitePool) -> Self {
        Self { pool }
    }

    pub async fn create_epic(&self, record: &HqEpicRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_epics (id, title, description, status, progress, plan_id, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
        )
        .bind(&record.id)
        .bind(&record.title)
        .bind(&record.description)
        .bind(&record.status)
        .bind(record.progress)
        .bind(&record.plan_id)
        .bind(record.created_at)
        .bind(record.updated_at)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_epics(&self) -> Result<Vec<HqEpicRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqEpicRecord>(
            "SELECT id, title, description, status, progress, plan_id, created_at, updated_at FROM hq_epics ORDER BY created_at DESC",
        )
        .fetch_all(&self.pool)
        .await
    }

    pub async fn get_epic(&self, id: &str) -> Result<Option<HqEpicRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqEpicRecord>(
            "SELECT id, title, description, status, progress, plan_id, created_at, updated_at FROM hq_epics WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn update_epic(&self, record: &HqEpicRecord) -> Result<u64, sqlx::Error> {
        let result = sqlx::query(
            "UPDATE hq_epics SET title = ?1, description = ?2, status = ?3, progress = ?4, plan_id = ?5, updated_at = ?6 WHERE id = ?7",
        )
        .bind(&record.title)
        .bind(&record.description)
        .bind(&record.status)
        .bind(record.progress)
        .bind(&record.plan_id)
        .bind(record.updated_at)
        .bind(&record.id)
        .execute(&self.pool)
        .await?;
        Ok(result.rows_affected())
    }

    pub async fn create_issue(&self, record: &HqIssueRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_issues (id, issue_number, identifier, title, description, status, priority, assignee_id, assignee_name, epic_id, phase_label, agent_turn, agent_max_turns, agent_live_action, is_live, files_changed_json, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18)",
        )
        .bind(&record.id)
        .bind(record.issue_number)
        .bind(&record.identifier)
        .bind(&record.title)
        .bind(&record.description)
        .bind(&record.status)
        .bind(&record.priority)
        .bind(&record.assignee_id)
        .bind(&record.assignee_name)
        .bind(&record.epic_id)
        .bind(&record.phase_label)
        .bind(record.agent_turn)
        .bind(record.agent_max_turns)
        .bind(&record.agent_live_action)
        .bind(record.is_live)
        .bind(&record.files_changed_json)
        .bind(record.created_at)
        .bind(record.updated_at)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_issues(
        &self,
        epic_id: Option<&str>,
    ) -> Result<Vec<HqIssueRecord>, sqlx::Error> {
        match epic_id {
            Some(epic_id) => {
                sqlx::query_as::<_, HqIssueRecord>(
                    "SELECT id, issue_number, identifier, title, description, status, priority, assignee_id, assignee_name, epic_id, phase_label, agent_turn, agent_max_turns, agent_live_action, is_live, files_changed_json, created_at, updated_at FROM hq_issues WHERE epic_id = ?1 ORDER BY created_at ASC",
                )
                .bind(epic_id)
                .fetch_all(&self.pool)
                .await
            }
            None => {
                sqlx::query_as::<_, HqIssueRecord>(
                    "SELECT id, issue_number, identifier, title, description, status, priority, assignee_id, assignee_name, epic_id, phase_label, agent_turn, agent_max_turns, agent_live_action, is_live, files_changed_json, created_at, updated_at FROM hq_issues ORDER BY created_at ASC",
                )
                .fetch_all(&self.pool)
                .await
            }
        }
    }

    pub async fn get_issue(&self, id: &str) -> Result<Option<HqIssueRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqIssueRecord>(
            "SELECT id, issue_number, identifier, title, description, status, priority, assignee_id, assignee_name, epic_id, phase_label, agent_turn, agent_max_turns, agent_live_action, is_live, files_changed_json, created_at, updated_at FROM hq_issues WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn update_issue(&self, record: &HqIssueRecord) -> Result<u64, sqlx::Error> {
        let result = sqlx::query(
            "UPDATE hq_issues SET title = ?1, description = ?2, status = ?3, priority = ?4, assignee_id = ?5, assignee_name = ?6, phase_label = ?7, agent_turn = ?8, agent_max_turns = ?9, agent_live_action = ?10, is_live = ?11, files_changed_json = ?12, updated_at = ?13 WHERE id = ?14",
        )
        .bind(&record.title)
        .bind(&record.description)
        .bind(&record.status)
        .bind(&record.priority)
        .bind(&record.assignee_id)
        .bind(&record.assignee_name)
        .bind(&record.phase_label)
        .bind(record.agent_turn)
        .bind(record.agent_max_turns)
        .bind(&record.agent_live_action)
        .bind(record.is_live)
        .bind(&record.files_changed_json)
        .bind(record.updated_at)
        .bind(&record.id)
        .execute(&self.pool)
        .await?;
        Ok(result.rows_affected())
    }

    pub async fn move_issue(
        &self,
        id: &str,
        status: &str,
        updated_at: i64,
    ) -> Result<u64, sqlx::Error> {
        let result = sqlx::query("UPDATE hq_issues SET status = ?1, updated_at = ?2 WHERE id = ?3")
            .bind(status)
            .bind(updated_at)
            .bind(id)
            .execute(&self.pool)
            .await?;
        Ok(result.rows_affected())
    }

    pub async fn next_issue_number(&self) -> Result<i64, sqlx::Error> {
        let row: (i64,) =
            sqlx::query_as("SELECT COALESCE(MAX(issue_number), 0) + 1 FROM hq_issues")
                .fetch_one(&self.pool)
                .await?;
        Ok(row.0)
    }

    pub async fn add_comment(&self, record: &HqCommentRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_comments (id, issue_id, author_name, author_role, author_icon, content, timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
        )
        .bind(&record.id)
        .bind(&record.issue_id)
        .bind(&record.author_name)
        .bind(&record.author_role)
        .bind(&record.author_icon)
        .bind(&record.content)
        .bind(record.timestamp)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_comments(&self, issue_id: &str) -> Result<Vec<HqCommentRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqCommentRecord>(
            "SELECT id, issue_id, author_name, author_role, author_icon, content, timestamp FROM hq_comments WHERE issue_id = ?1 ORDER BY timestamp ASC",
        )
        .bind(issue_id)
        .fetch_all(&self.pool)
        .await
    }

    pub async fn save_plan(&self, record: &HqPlanRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_plans (id, epic_id, title, status, director_description, plan_json, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
             ON CONFLICT(epic_id) DO UPDATE SET id = excluded.id, title = excluded.title, status = excluded.status, director_description = excluded.director_description, plan_json = excluded.plan_json, updated_at = excluded.updated_at",
        )
        .bind(&record.id)
        .bind(&record.epic_id)
        .bind(&record.title)
        .bind(&record.status)
        .bind(&record.director_description)
        .bind(&record.plan_json)
        .bind(record.created_at)
        .bind(record.updated_at)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn get_plan(&self, id: &str) -> Result<Option<HqPlanRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqPlanRecord>(
            "SELECT id, epic_id, title, status, director_description, plan_json, created_at, updated_at FROM hq_plans WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn get_plan_by_epic(
        &self,
        epic_id: &str,
    ) -> Result<Option<HqPlanRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqPlanRecord>(
            "SELECT id, epic_id, title, status, director_description, plan_json, created_at, updated_at FROM hq_plans WHERE epic_id = ?1 ORDER BY created_at DESC LIMIT 1",
        )
        .bind(epic_id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn update_plan_status(
        &self,
        id: &str,
        status: &str,
        updated_at: i64,
    ) -> Result<u64, sqlx::Error> {
        let result = sqlx::query("UPDATE hq_plans SET status = ?1, updated_at = ?2 WHERE id = ?3")
            .bind(status)
            .bind(updated_at)
            .bind(id)
            .execute(&self.pool)
            .await?;
        Ok(result.rows_affected())
    }

    pub async fn upsert_agent(&self, record: &HqAgentRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_agents (id, name, role, tier, model, status, icon, parent_id, current_task, current_issue_id, turn, max_turns, assigned_issue_ids_json, files_touched_json, total_cost_usd, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)
             ON CONFLICT(id) DO UPDATE SET name = excluded.name, role = excluded.role, tier = excluded.tier, model = excluded.model, status = excluded.status, icon = excluded.icon, parent_id = excluded.parent_id, current_task = excluded.current_task, current_issue_id = excluded.current_issue_id, turn = excluded.turn, max_turns = excluded.max_turns, assigned_issue_ids_json = excluded.assigned_issue_ids_json, files_touched_json = excluded.files_touched_json, total_cost_usd = excluded.total_cost_usd, updated_at = excluded.updated_at",
        )
        .bind(&record.id)
        .bind(&record.name)
        .bind(&record.role)
        .bind(&record.tier)
        .bind(&record.model)
        .bind(&record.status)
        .bind(&record.icon)
        .bind(&record.parent_id)
        .bind(&record.current_task)
        .bind(&record.current_issue_id)
        .bind(record.turn)
        .bind(record.max_turns)
        .bind(&record.assigned_issue_ids_json)
        .bind(&record.files_touched_json)
        .bind(record.total_cost_usd)
        .bind(record.created_at)
        .bind(record.updated_at)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_agents(&self) -> Result<Vec<HqAgentRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqAgentRecord>(
            "SELECT id, name, role, tier, model, status, icon, parent_id, current_task, current_issue_id, turn, max_turns, assigned_issue_ids_json, files_touched_json, total_cost_usd, created_at, updated_at FROM hq_agents ORDER BY created_at ASC",
        )
        .fetch_all(&self.pool)
        .await
    }

    pub async fn get_agent(&self, id: &str) -> Result<Option<HqAgentRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqAgentRecord>(
            "SELECT id, name, role, tier, model, status, icon, parent_id, current_task, current_issue_id, turn, max_turns, assigned_issue_ids_json, files_touched_json, total_cost_usd, created_at, updated_at FROM hq_agents WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn append_agent_transcript(
        &self,
        record: &HqAgentTranscriptRecord,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_agent_transcript (id, agent_id, entry_type, tool_name, tool_path, tool_status, content, timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
        )
        .bind(&record.id)
        .bind(&record.agent_id)
        .bind(&record.entry_type)
        .bind(&record.tool_name)
        .bind(&record.tool_path)
        .bind(&record.tool_status)
        .bind(&record.content)
        .bind(record.timestamp)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_agent_transcript(
        &self,
        agent_id: &str,
    ) -> Result<Vec<HqAgentTranscriptRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqAgentTranscriptRecord>(
            "SELECT id, agent_id, entry_type, tool_name, tool_path, tool_status, content, timestamp FROM hq_agent_transcript WHERE agent_id = ?1 ORDER BY timestamp ASC",
        )
        .bind(agent_id)
        .fetch_all(&self.pool)
        .await
    }

    pub async fn create_activity(&self, record: &HqActivityRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_activity (id, event_type, agent_name, message, color, timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
        )
        .bind(&record.id)
        .bind(&record.event_type)
        .bind(&record.agent_name)
        .bind(&record.message)
        .bind(&record.color)
        .bind(record.timestamp)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_activity(&self, limit: i64) -> Result<Vec<HqActivityRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqActivityRecord>(
            "SELECT id, event_type, agent_name, message, color, timestamp FROM hq_activity ORDER BY timestamp DESC LIMIT ?1",
        )
        .bind(limit)
        .fetch_all(&self.pool)
        .await
    }

    pub async fn add_chat_message(&self, record: &HqChatMessageRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO hq_chat_messages (id, role, content, delegations_json, epic_id, timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
        )
        .bind(&record.id)
        .bind(&record.role)
        .bind(&record.content)
        .bind(&record.delegations_json)
        .bind(&record.epic_id)
        .bind(record.timestamp)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn list_chat_messages(
        &self,
        limit: i64,
    ) -> Result<Vec<HqChatMessageRecord>, sqlx::Error> {
        sqlx::query_as::<_, HqChatMessageRecord>(
            "SELECT id, role, content, delegations_json, epic_id, timestamp FROM hq_chat_messages ORDER BY timestamp ASC LIMIT ?1",
        )
        .bind(limit)
        .fetch_all(&self.pool)
        .await
    }

    pub async fn delete_chat_messages_by_content(
        &self,
        contents: &[&str],
    ) -> Result<u64, sqlx::Error> {
        if contents.is_empty() {
            return Ok(0);
        }
        let placeholders: Vec<String> = (1..=contents.len()).map(|i| format!("?{i}")).collect();
        let sql = format!(
            "DELETE FROM hq_chat_messages WHERE content IN ({})",
            placeholders.join(", ")
        );
        let mut query = sqlx::query(&sql);
        for content in contents {
            query = query.bind(*content);
        }
        let result = query.execute(&self.pool).await?;
        Ok(result.rows_affected())
    }
}
