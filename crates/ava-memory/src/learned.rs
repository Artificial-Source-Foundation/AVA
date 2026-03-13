use rusqlite::{params, Connection, OptionalExtension, Result};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LearnedMemoryStatus {
    Pending,
    Confirmed,
    Rejected,
}

impl LearnedMemoryStatus {
    pub(crate) fn as_str(self) -> &'static str {
        match self {
            Self::Pending => "pending",
            Self::Confirmed => "confirmed",
            Self::Rejected => "rejected",
        }
    }

    pub(crate) fn from_db(value: &str) -> Self {
        match value {
            "confirmed" => Self::Confirmed,
            "rejected" => Self::Rejected,
            _ => Self::Pending,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct LearnedMemory {
    pub id: i64,
    pub key: String,
    pub value: String,
    pub source_excerpt: String,
    pub observed_count: i64,
    pub confidence: f64,
    pub status: LearnedMemoryStatus,
    pub created_at: String,
    pub updated_at: String,
}

pub(crate) fn upsert_observation(
    conn: &Connection,
    key: &str,
    value: &str,
    source_excerpt: &str,
    confidence: f64,
) -> Result<LearnedMemory> {
    let existing = conn
        .query_row(
            "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
             FROM learned_memories
             WHERE key = ?1 AND value = ?2
             LIMIT 1",
            params![key, value],
            row_to_learned_memory,
        )
        .optional()?;

    if let Some(existing) = existing {
        if existing.status == LearnedMemoryStatus::Rejected {
            return Ok(existing);
        }

        let next_count = existing.observed_count + 1;
        let next_confidence = existing.confidence.max(confidence).min(0.99);
        let next_status = if existing.status == LearnedMemoryStatus::Pending
            && next_count >= 2
            && next_confidence >= 0.7
        {
            LearnedMemoryStatus::Confirmed
        } else {
            existing.status
        };

        conn.execute(
            "UPDATE learned_memories
             SET source_excerpt = ?1,
                 observed_count = ?2,
                 confidence = ?3,
                 status = ?4,
                 updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
             WHERE id = ?5",
            params![
                source_excerpt,
                next_count,
                next_confidence,
                next_status.as_str(),
                existing.id
            ],
        )?;

        conn.query_row(
            "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
             FROM learned_memories
             WHERE id = ?1",
            params![existing.id],
            row_to_learned_memory,
        )
    } else {
        let initial_confidence = confidence.min(0.99);
        conn.execute(
            "INSERT INTO learned_memories (key, value, source_excerpt, observed_count, confidence, status)
             VALUES (?1, ?2, ?3, 1, ?4, 'pending')",
            params![key, value, source_excerpt, initial_confidence],
        )?;

        let id = conn.last_insert_rowid();
        conn.query_row(
            "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
             FROM learned_memories
             WHERE id = ?1",
            params![id],
            row_to_learned_memory,
        )
    }
}

pub(crate) fn set_status(
    conn: &Connection,
    id: i64,
    status: LearnedMemoryStatus,
) -> Result<Option<LearnedMemory>> {
    conn.execute(
        "UPDATE learned_memories
         SET status = ?1,
             updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
         WHERE id = ?2",
        params![status.as_str(), id],
    )?;

    conn.query_row(
        "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
         FROM learned_memories
         WHERE id = ?1",
        params![id],
        row_to_learned_memory,
    )
    .optional()
}

pub(crate) fn list(
    conn: &Connection,
    status: Option<LearnedMemoryStatus>,
    limit: usize,
) -> Result<Vec<LearnedMemory>> {
    if let Some(status) = status {
        let mut stmt = conn.prepare(
            "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
             FROM learned_memories
             WHERE status = ?1
             ORDER BY updated_at DESC, id DESC
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(
            params![status.as_str(), limit as i64],
            row_to_learned_memory,
        )?;
        rows.collect()
    } else {
        let mut stmt = conn.prepare(
            "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
             FROM learned_memories
             ORDER BY updated_at DESC, id DESC
             LIMIT ?1",
        )?;
        let rows = stmt.query_map(params![limit as i64], row_to_learned_memory)?;
        rows.collect()
    }
}

pub(crate) fn search_confirmed(
    conn: &Connection,
    query: &str,
    limit: usize,
) -> Result<Vec<LearnedMemory>> {
    let lowered = query.to_lowercase();
    let mut terms: Vec<String> = lowered
        .split_whitespace()
        .map(|term| term.to_string())
        .collect();
    terms.truncate(5);

    if terms.is_empty() {
        return Ok(Vec::new());
    }

    let mut out = Vec::new();
    for term in terms {
        let escaped = term.replace('%', "\\%").replace('_', "\\_");
        let like_term = format!("%{escaped}%");
        let mut stmt = conn.prepare(
            "SELECT id, key, value, source_excerpt, observed_count, confidence, status, created_at, updated_at
             FROM learned_memories
             WHERE status = 'confirmed'
               AND (LOWER(key) LIKE ?1 ESCAPE '\\' OR LOWER(value) LIKE ?1 ESCAPE '\\')
             ORDER BY confidence DESC, updated_at DESC, id DESC
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(params![like_term, limit as i64], row_to_learned_memory)?;
        for row in rows {
            let row = row?;
            if !out
                .iter()
                .any(|existing: &LearnedMemory| existing.id == row.id)
            {
                out.push(row);
            }
            if out.len() >= limit {
                return Ok(out);
            }
        }
    }

    Ok(out)
}

fn row_to_learned_memory(row: &rusqlite::Row<'_>) -> Result<LearnedMemory> {
    Ok(LearnedMemory {
        id: row.get(0)?,
        key: row.get(1)?,
        value: row.get(2)?,
        source_excerpt: row.get(3)?,
        observed_count: row.get(4)?,
        confidence: row.get(5)?,
        status: LearnedMemoryStatus::from_db(&row.get::<_, String>(6)?),
        created_at: row.get(7)?,
        updated_at: row.get(8)?,
    })
}
