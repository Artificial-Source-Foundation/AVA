use ava_memory::{Memory, MemorySystem};
use serde::Deserialize;
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MemoryOutput {
    pub id: i64,
    pub key: String,
    pub value: String,
    pub created_at: String,
}

impl From<Memory> for MemoryOutput {
    fn from(value: Memory) -> Self {
        Self {
            id: value.id,
            key: value.key,
            value: value.value,
            created_at: value.created_at,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MemoryRememberInput {
    pub db_path: String,
    pub key: String,
    pub value: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MemoryRecallInput {
    pub db_path: String,
    pub key: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MemorySearchInput {
    pub db_path: String,
    pub query: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MemoryRecentInput {
    pub db_path: String,
    pub limit: usize,
}

fn open_memory_system(db_path: &str) -> Result<MemorySystem, String> {
    MemorySystem::new(db_path).map_err(|err| err.to_string())
}

#[tauri::command]
pub fn memory_remember(input: MemoryRememberInput) -> Result<MemoryOutput, String> {
    let system = open_memory_system(&input.db_path)?;
    system
        .remember(&input.key, &input.value)
        .map(MemoryOutput::from)
        .map_err(|err| err.to_string())
}

#[tauri::command]
pub fn memory_recall(input: MemoryRecallInput) -> Result<Option<MemoryOutput>, String> {
    let system = open_memory_system(&input.db_path)?;
    system
        .recall(&input.key)
        .map(|maybe_memory| maybe_memory.map(MemoryOutput::from))
        .map_err(|err| err.to_string())
}

#[tauri::command]
pub fn memory_search(input: MemorySearchInput) -> Result<Vec<MemoryOutput>, String> {
    let system = open_memory_system(&input.db_path)?;
    system
        .search(&input.query)
        .map(|memories| memories.into_iter().map(MemoryOutput::from).collect())
        .map_err(|err| err.to_string())
}

#[tauri::command]
pub fn memory_recent(input: MemoryRecentInput) -> Result<Vec<MemoryOutput>, String> {
    let system = open_memory_system(&input.db_path)?;
    system
        .get_recent(input.limit)
        .map(|memories| memories.into_iter().map(MemoryOutput::from).collect())
        .map_err(|err| err.to_string())
}

#[cfg(test)]
mod tests {
    use super::{
        memory_recall, memory_recent, memory_remember, memory_search, MemoryRecallInput,
        MemoryRecentInput, MemoryRememberInput, MemorySearchInput,
    };
    use serde_json::json;
    use std::path::PathBuf;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_db_path(test_name: &str) -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock should be monotonic")
            .as_nanos();
        std::env::temp_dir().join(format!("ava-{test_name}-{stamp}.sqlite3"))
    }

    #[test]
    fn memory_commands_accept_json_mapped_args_and_serialize_outputs() {
        let db_path = temp_db_path("memory_command_adapter");
        let remember_input: MemoryRememberInput = serde_json::from_value(json!({
            "dbPath": db_path.to_string_lossy().to_string(),
            "key": "project",
            "value": "ava"
        }))
        .expect("remember input should deserialize");

        let remembered = memory_remember(remember_input).expect("remember should succeed");

        let recalled = memory_recall(MemoryRecallInput {
            db_path: db_path.to_string_lossy().to_string(),
            key: "project".to_string(),
        })
        .expect("recall should succeed")
        .expect("memory should exist");

        let search_results = memory_search(MemorySearchInput {
            db_path: db_path.to_string_lossy().to_string(),
            query: "ava".to_string(),
        })
        .expect("search should succeed");

        let remembered_json =
            serde_json::to_value(&remembered).expect("remember output should serialize");
        assert_eq!(remembered_json["key"], "project");
        assert_eq!(recalled.value, "ava");
        assert_eq!(search_results.len(), 1);
    }

    #[test]
    fn memory_recent_returns_serializable_output() {
        let db_path = temp_db_path("memory_recent_adapter");
        let db_path_str = db_path.to_string_lossy().to_string();

        let _ = memory_remember(MemoryRememberInput {
            db_path: db_path_str.clone(),
            key: "project".to_string(),
            value: "ava".to_string(),
        })
        .expect("remember should succeed");

        let recent_input: MemoryRecentInput = serde_json::from_value(json!({
            "dbPath": db_path_str,
            "limit": 10
        }))
        .expect("recent input should deserialize");

        let recent = memory_recent(recent_input).expect("recent should succeed");
        let recent_json = serde_json::to_value(&recent).expect("recent output should serialize");

        assert!(recent_json.as_array().is_some());
        assert_eq!(recent.len(), 1);
        assert_eq!(recent[0].key, "project");
    }
}
