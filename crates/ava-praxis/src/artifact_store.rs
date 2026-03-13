use std::fs;
use std::path::{Path, PathBuf};
use uuid::Uuid;

use ava_types::Result;

use crate::artifact::Artifact;

#[derive(Debug, Default)]
pub struct ArtifactStore {
    artifacts: Vec<Artifact>,
}

impl ArtifactStore {
    pub fn add(&mut self, artifact: Artifact) {
        self.artifacts.push(artifact);
    }

    pub fn get(&self, id: Uuid) -> Option<&Artifact> {
        self.artifacts.iter().find(|a| a.id == id)
    }

    pub fn list(&self) -> &[Artifact] {
        &self.artifacts
    }

    pub fn list_for_spec(&self, spec_id: Uuid) -> Vec<&Artifact> {
        self.artifacts
            .iter()
            .filter(|a| a.spec_id == Some(spec_id))
            .collect()
    }
}

#[derive(Debug)]
pub struct FileArtifactStore {
    path: PathBuf,
    inner: ArtifactStore,
}

impl FileArtifactStore {
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref().to_path_buf();
        let inner = if path.exists() {
            let raw = fs::read_to_string(&path)?;
            if raw.trim().is_empty() {
                ArtifactStore::default()
            } else {
                let artifacts: Vec<Artifact> = serde_json::from_str(&raw)?;
                ArtifactStore { artifacts }
            }
        } else {
            ArtifactStore::default()
        };

        Ok(Self { path, inner })
    }

    pub fn add(&mut self, artifact: Artifact) -> Result<()> {
        self.inner.add(artifact);
        self.flush()
    }

    pub fn get(&self, id: Uuid) -> Option<&Artifact> {
        self.inner.get(id)
    }

    pub fn list(&self) -> &[Artifact] {
        self.inner.list()
    }

    pub fn list_for_spec(&self, spec_id: Uuid) -> Vec<&Artifact> {
        self.inner.list_for_spec(spec_id)
    }

    fn flush(&self) -> Result<()> {
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)?;
        }
        let serialized = serde_json::to_string_pretty(self.inner.list())?;
        fs::write(&self.path, serialized)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::artifact::{Artifact, ArtifactKind};

    #[test]
    fn artifact_store_filters_by_spec() {
        let mut store = ArtifactStore::default();
        let spec_a = Uuid::new_v4();
        let spec_b = Uuid::new_v4();

        store.add(Artifact::new(
            ArtifactKind::Custom("note".to_string()),
            "worker-1",
            "A",
            "first",
            Some(spec_a),
        ));
        store.add(Artifact::new(
            ArtifactKind::Custom("note".to_string()),
            "worker-2",
            "B",
            "second",
            Some(spec_b),
        ));

        let a_items = store.list_for_spec(spec_a);
        assert_eq!(a_items.len(), 1);
        assert_eq!(a_items[0].title, "A");
    }

    #[test]
    fn file_artifact_store_roundtrip() {
        let temp =
            std::env::temp_dir().join(format!("ava-praxis-artifacts-{}.json", Uuid::new_v4()));
        let mut store = FileArtifactStore::open(&temp).expect("open store");

        let artifact = Artifact::new(
            ArtifactKind::WorkflowSummary,
            "workflow",
            "summary",
            "done",
            None,
        );
        let id = artifact.id;
        store.add(artifact).expect("persist artifact");

        let reopened = FileArtifactStore::open(&temp).expect("reopen store");
        let loaded = reopened.get(id).expect("artifact persisted");
        assert_eq!(loaded.title, "summary");
        assert_eq!(reopened.list().len(), 1);

        let _ = fs::remove_file(temp);
    }
}
