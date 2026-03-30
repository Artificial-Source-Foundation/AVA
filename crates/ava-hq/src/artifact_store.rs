use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};
use uuid::Uuid;

use ava_types::{AvaError, Result};

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
                match serde_json::from_str(&raw) {
                    Ok(artifacts) => ArtifactStore { artifacts },
                    Err(error) => {
                        quarantine_corrupt_store(&path)?;
                        tracing::warn!(
                            path = %path.display(),
                            error = %error,
                            "artifact store was corrupt; quarantined and reopened empty"
                        );
                        ArtifactStore::default()
                    }
                }
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
        write_file_atomic(&self.path, &serialized)?;
        Ok(())
    }
}

fn quarantine_corrupt_store(path: &Path) -> Result<()> {
    let file_name = path.file_name().ok_or_else(|| {
        AvaError::IoError(format!(
            "could not determine file name for {}",
            path.display()
        ))
    })?;
    let quarantined = path.with_file_name(format!(
        ".{}.corrupt.{}",
        file_name.to_string_lossy(),
        Uuid::new_v4()
    ));
    fs::rename(path, quarantined)?;
    Ok(())
}

fn write_file_atomic(path: &Path, content: &str) -> Result<()> {
    let file_name = path.file_name().ok_or_else(|| {
        AvaError::IoError(format!(
            "could not determine file name for {}",
            path.display()
        ))
    })?;
    let temp_path = path.with_file_name(format!(
        ".{}.{}.tmp",
        file_name.to_string_lossy(),
        Uuid::new_v4()
    ));
    struct TempFileCleanup {
        path: PathBuf,
        keep: bool,
    }

    impl Drop for TempFileCleanup {
        fn drop(&mut self) {
            if !self.keep {
                let _ = fs::remove_file(&self.path);
            }
        }
    }

    let mut cleanup = TempFileCleanup {
        path: temp_path.clone(),
        keep: false,
    };

    let mut temp_file = File::create(&temp_path)?;
    temp_file.write_all(content.as_bytes())?;
    temp_file.sync_all()?;
    drop(temp_file);

    fs::rename(&temp_path, path)?;
    cleanup.keep = true;

    if let Some(parent) = path.parent() {
        if let Ok(dir) = File::open(parent) {
            let _ = dir.sync_all();
        }
    }

    Ok(())
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
        let temp = std::env::temp_dir().join(format!("ava-hq-artifacts-{}.json", Uuid::new_v4()));
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

    #[test]
    fn atomic_write_replaces_existing_content() {
        let temp = std::env::temp_dir().join(format!("ava-hq-artifacts-{}.json", Uuid::new_v4()));
        fs::write(&temp, "old").expect("seed file");

        write_file_atomic(&temp, "new").expect("atomic write succeeds");

        let content = fs::read_to_string(&temp).expect("read file");
        assert_eq!(content, "new");

        let _ = fs::remove_file(temp);
    }

    #[test]
    fn corrupt_store_is_quarantined_and_reopened_empty() {
        let dir = tempfile::tempdir().expect("tempdir");
        let path = dir.path().join("artifacts.json");
        fs::write(&path, "{\"broken\":").expect("seed corrupt file");

        let store = FileArtifactStore::open(&path).expect("open succeeds with recovery");

        assert!(store.list().is_empty());
        assert!(!path.exists(), "corrupt file should be moved aside");

        let quarantined: Vec<_> = fs::read_dir(dir.path())
            .expect("read dir")
            .filter_map(|entry| entry.ok())
            .map(|entry| entry.file_name().to_string_lossy().into_owned())
            .filter(|name| name.starts_with(".artifacts.json.corrupt."))
            .collect();
        assert_eq!(quarantined.len(), 1);
    }
}
