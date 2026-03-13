use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum SpecStatus {
    Draft,
    Approved,
    Archived,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SpecTask {
    pub id: Uuid,
    pub title: String,
    pub done: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SpecDocument {
    pub id: Uuid,
    pub title: String,
    pub requirements: String,
    pub design: String,
    pub tasks: Vec<SpecTask>,
    pub status: SpecStatus,
}

impl SpecDocument {
    pub fn new(
        title: impl Into<String>,
        requirements: impl Into<String>,
        design: impl Into<String>,
        tasks: Vec<String>,
    ) -> Self {
        Self {
            id: Uuid::new_v4(),
            title: title.into(),
            requirements: requirements.into(),
            design: design.into(),
            tasks: tasks
                .into_iter()
                .map(|title| SpecTask {
                    id: Uuid::new_v4(),
                    title,
                    done: false,
                })
                .collect(),
            status: SpecStatus::Draft,
        }
    }
}

#[derive(Debug, Default)]
pub struct SpecStore {
    specs: HashMap<Uuid, SpecDocument>,
}

impl SpecStore {
    pub fn create(&mut self, spec: SpecDocument) -> Uuid {
        let id = spec.id;
        self.specs.insert(id, spec);
        id
    }

    pub fn get(&self, id: Uuid) -> Option<&SpecDocument> {
        self.specs.get(&id)
    }

    pub fn list(&self) -> Vec<&SpecDocument> {
        self.specs.values().collect()
    }

    pub fn set_status(&mut self, id: Uuid, status: SpecStatus) -> bool {
        let Some(spec) = self.specs.get_mut(&id) else {
            return false;
        };
        spec.status = status;
        true
    }

    pub fn complete_task(&mut self, spec_id: Uuid, task_id: Uuid) -> bool {
        let Some(spec) = self.specs.get_mut(&spec_id) else {
            return false;
        };
        let Some(task) = spec.tasks.iter_mut().find(|task| task.id == task_id) else {
            return false;
        };
        task.done = true;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn spec_store_roundtrip() {
        let mut store = SpecStore::default();
        let spec = SpecDocument::new(
            "Auth Refactor",
            "Split auth service boundaries",
            "Extract token validation module",
            vec!["Create service".to_string(), "Add tests".to_string()],
        );
        let id = store.create(spec);
        let saved = store.get(id).expect("spec exists");
        assert_eq!(saved.title, "Auth Refactor");
        assert_eq!(saved.status, SpecStatus::Draft);
        assert_eq!(saved.tasks.len(), 2);
    }

    #[test]
    fn spec_task_completion_and_status_update() {
        let mut store = SpecStore::default();
        let spec = SpecDocument::new("X", "R", "D", vec!["T1".to_string()]);
        let task_id = spec.tasks[0].id;
        let spec_id = store.create(spec);

        assert!(store.complete_task(spec_id, task_id));
        assert!(store.set_status(spec_id, SpecStatus::Approved));

        let spec = store.get(spec_id).expect("spec exists");
        assert!(spec.tasks[0].done);
        assert_eq!(spec.status, SpecStatus::Approved);
    }
}
